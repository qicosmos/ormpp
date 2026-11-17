# PostgreSQL async validation record

Local validation date: 2026-11-17. Current implementation:
`73cf0dc2e9adbfd7db6678ae03f903f9f222be91`. The correction starts from
`b6b484ae50510dc17c799ccc1dbb1101b3415eaf`, whose published checks completed
successfully before the execution model was narrowed to the user's requirement.
Later validation documentation commits do not change the implementation.
GitHub Actions results for the latest published head are recorded on PR #288,
separately from the local results below.

## Supported execution model

One `io_context` is run by one thread. Connections, diagnostics and cancellation
stay on that owning thread. Different threads use independent contexts and
connections. Multiple connections can share the same single-worker context.
The backend has no connection mutex, atomic state fields, strand, operation
channel or per-operation `co_spawn`. Awaitables execute directly on the caller's
executor. Ordinary `bool` state rejects overlapping operations on one connection
without queueing; explicit transactions require an exclusive connection lease.

Owned parameters and retained connection state still survive suspension and
public-handle destruction on the owning thread. Socket readiness wrappers release
the borrowed descriptor before libpq calls/close. Resolver completions, libpq,
result mapping and cleanup stay on the owning context; no executor migration is
performed by the backend. The generic pool's existing strand implementation is
unchanged, as is `mysql_async` itself.

## Deterministic before/after regression

The new `postgresql async single-thread ownership and overlap` test was compiled
against an archive of pre-correction `b6b484a`. It **failed** because the old
operation channel queued a second ping/disconnect instead of rejecting them.
The same test passes on `73cf0dc`.

The test uses a blocker connection holding an advisory lock. An observer queries
`pg_locks` until it sees the target PID waiting on an ungranted lock, proving the
target query has suspended. It then submits overlapping ping/disconnect calls.
A FIFO executor post observes their completions before the blocker is released;
no sleeps or elapsed-time assumptions establish that ordering. The public target
handle is destroyed while its original query is still pending. Finally the lock
is released and all operations are joined before assertions are reported.

Separate cancellation/reconnect and large-temporary-buffer tests remain in the
suite. Another test synchronizes two threads with `std::barrier`; each thread
creates and runs its own context and database connection. The previous test that
ran one context with four worker threads is intentionally replaced: that model
is outside the supported contract. PostgreSQL async tests have no exclusions.

Before/after artifacts are retained under
`/tmp/ormpp-pg-async-validation/single-thread` (a directory on the data disk):
`pre-fix-regression.log`, `build-pre-fix.log`, configuration/build/test logs and
`matrix.json`.

## Current local results

All six configurations below passed with each of the three transports, for
18 successful PostgreSQL async runs. Each run passed all 6 cases without
exclusions. The example also built and ran successfully with verified TLS.
ASan and TSan results are separate, including both single-worker coroutine
interleaving and simultaneous independent contexts on different threads.

Environment: Ubuntu 24.04, GCC 13.3, Clang 18.1.3, standalone Asio 1.36.0,
libpq/PostgreSQL 16.15. A dedicated server listens on `127.0.0.1:55432`, with
password authentication and a local TLS certificate valid for `localhost` and
`127.0.0.1`.

| Build suffix | Compiler / build | Sanitizer | Synchronous PG enabled |
| --- | --- | --- | --- |
| gcc-release | GCC / Release | off | no |
| gcc-debug | GCC / Debug | off | no |
| clang-release | Clang / Release | off | yes |
| clang-debug | Clang / Debug | off | no |
| clang-asan | Clang / Debug | AddressSanitizer | yes |
| clang-tsan | Clang / Debug | ThreadSanitizer | yes |

Each matrix entry runs the full PostgreSQL async target with `disable`, `require`
and `verify-full`. Tests inspect `pg_stat_ssl` to check the actual transport.
The ASan and TSan executables are built separately; an ASan pass is not used as
evidence of race freedom.

Example commands (substitute the compiler/build/sanitizer flags from the table):

```sh
cmake -S /data/code/ormpp -B /tmp/ormpp-pg-async-validation/build-clang-asan \
  -DCMAKE_C_COMPILER=clang-18 -DCMAKE_CXX_COMPILER=clang++-18 \
  -DCMAKE_BUILD_TYPE=Debug -DENABLE_PG_ASYNC=ON -DENABLE_PG=ON \
  -DORMPP_ASIO_INCLUDE_DIR=/data/code/cinatra/include -DBUILD_EXAMPLES=OFF \
  -DENABLE_SANITIZER=ON -DENABLE_THREAD_SANITIZER=OFF
cmake --build /tmp/ormpp-pg-async-validation/build-clang-asan \
  --target test_postgresql_async -j2
ORMPP_ASYNC_PG_HOST=localhost ORMPP_ASYNC_PG_PORT=55432 PGPORT=55432 \
  PGSSLMODE=verify-full PGSSLROOTCERT=/tmp/ormpp-pg-async-data/server.crt \
  ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 TSAN_OPTIONS=halt_on_error=1 \
  ctest --test-dir /tmp/ormpp-pg-async-validation/build-clang-asan \
  -R '^test_postgresql_async$' -V
```

For TSan, use `-DENABLE_SANITIZER=OFF -DENABLE_THREAD_SANITIZER=ON` in a separate
build directory. Unsanitized builds set both flags OFF. GCC configurations use
`cc`/`c++`, and Release uses `-DCMAKE_BUILD_TYPE=Release`. Test credentials default
to `root` / `123456` / `test_ormppdb`; override them through `ORMPP_ASYNC_PG_USER`,
`ORMPP_ASYNC_PG_PASSWORD` and `ORMPP_ASYNC_PG_DB`. Repeat the test with
`PGSSLMODE=disable` and `require`.

The script `single-thread/run_matrix.py` records exact build/test commands and
results in `single-thread/matrix.json`. The example is built in the Clang Release
configuration with `BUILD_EXAMPLES=ON` and executed with `PGHOST=localhost`,
`PGPORT=55432`, `PGUSER=root`, `PGDATABASE=test_ormppdb`, password authentication,
`PGSSLMODE=verify-full` and the same `PGSSLROOTCERT`.

The current change affects only the PostgreSQL backend, its tests and docs;
unchanged synchronous suites are checked by CI. They were not rerun locally for
this correction. MySQL/MariaDB, SQLCipher and non-Linux configurations are not
claimed as locally validated here.

## Existing synchronous SQLite TSan exclusion

The unfiltered synchronous TSan suite reports a race in the unchanged test
`issue #238: multi-thread concurrent first query`. Its workers initialize
independent SQLite connections, whose `sqlite::reset_error()` writes shared
static `has_error_` and diagnostic storage. No PostgreSQL async code is involved.

The original test was built from base `c5a63f5bb5ccae8740754d4c744815876ff7656f`
under Clang TSan (`ENABLE_PG=ON`, `ENABLE_SANITIZER=OFF`,
`ENABLE_THREAD_SANITIZER=ON`). It failed with exit 66 and this report:

```text
WARNING: ThreadSanitizer: data race
ormpp::sqlite::reset_error() .../baseline/ormpp/sqlite.hpp:26:16
ormpp::sqlite::connect(...) .../baseline/ormpp/sqlite.hpp:41:5
Location is global 'ormpp::sqlite::has_error_'
```

A second baseline probe used `std::barrier(2)` immediately before two calls to
`sqlite::reset_error()` and reproduced the same race without sleeps or a database.
Logs remain in the parent validation directory:
`baseline-existing-clang-tsan-238.log`, `baseline-sqlite-error-race.log` and the
unfiltered implementation run `final-existing-clang-tsan.log`.

The synchronous CI TSan step excludes exactly that case:

```sh
PGPORT=55432 PGSSLMODE=disable TSAN_OPTIONS=halt_on_error=1 \
  /tmp/ormpp-pg-async-validation/build-clang-tsan/tests/test_ormpp \
  '--test-case-exclude=issue #238: multi-thread concurrent first query'
```

The previous implementation validated 63 cases / 796 assertions with this
exclusion, and 64 / 799 under ASan without exclusions. This is not an unfiltered
synchronous-suite TSan pass. PostgreSQL async TSan has no excluded cases.

## Earlier feature and CI evidence

The feature and character-array probes failed to compile at original base
`c5a63f5`, and passed after implementation. The feature probe proves the backend
was previously absent; it is not evidence of a previous concurrency bug.
Historical local results for `5ed42b6` and `957d2fe` remain in the parent directory;
their shared-context/multiple-worker claims are superseded by the model above.

CI exposed a required `<coroutine>` dependency in shared async traits on GCC 15
([original failure](https://github.com/qicosmos/ormpp/actions/runs/43986043833/job/133714325856)).
The header now includes it directly. YAML block-scalar quoting preserves the
`#238` test filter. The coverage/native MySQL workflows explicitly configure
temporary TCP test accounts after root Unix-socket authentication caused failures
([original coverage failure](https://github.com/qicosmos/ormpp/actions/runs/43986043705/job/133714325429)).

Legacy MySQL Linux workflows remain on Ubuntu 24.04 / GCC 13.3, matching their
[successful baseline](https://github.com/qicosmos/ormpp/actions/runs/37261003253/job/111608021742).
The automatically upgraded Ubuntu 26.04 / GCC 15.2 runner had crashed the unchanged
legacy Release smoke test ([failure](https://github.com/qicosmos/ormpp/actions/runs/43987259770/job/133718508939)).
This is a CI compatibility pin, not a claimed fix of that client's GCC 15 behavior.
PostgreSQL CI remains on `ubuntu-latest`, including GCC 15 and separate ASan/TSan.
Before the single-thread correction, head `b6b484a` completed all 70 checks / 18
push/PR workflows successfully; those results are historical, not results for the
current correction.
