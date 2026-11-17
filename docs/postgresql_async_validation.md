# PostgreSQL async validation record

Local validation date: 2026-11-17. The prepared-query and connection-pool update
was tested at `ee310b0e850e2b2d6ad455cf12058d3c78e6a344`, starting from published
head `e9719dacebcfa1ab46c347bfd56b5aadc80a9dd8`. The earlier single-thread
correction was tested at `73cf0dc2e9adbfd7db6678ae03f903f9f222be91`, starting from
`b6b484ae50510dc17c799ccc1dbb1101b3415eaf`. Documentation-only commits do not
change those tested implementations.
GitHub Actions results for the latest published head are recorded on PR #288,
separately from the local results below.

## Prepared-query and pool update

`query_prepared_s` is opt-in, including its `dbng` forwarding method. Each
connection keeps at most 128 entries keyed by SQL and parameter OIDs. Preparation
uses `PQsendPrepare`, subsequent execution uses `PQsendQueryPrepared`, and
parameters remain owned across both exchanges. No parameter values or result
rows are cached. New statements beyond the limit use an uncached parameterized
exchange. Closing/reconnecting clears the cache; ordinary SQL errors are drained
without retrying the statement. This adds no mutex, strand or executor migration
to the PostgreSQL connection.

The generic pool retains its existing strand. Failed reconnects now preserve
fixed-pool capacity; returned connections notify existing waiters in order.
Queued cancellation and close release those waiters. The new
`pool_options::ping_on_checkout` defaults to the existing SQL ping; opting out
uses a backend's local `is_open()` probe, falling back to ping when unsupported.
The PostgreSQL probe consumes buffered FATAL/EOF/TLS shutdown notifications
without sending SQL.

Local validation of `ee310b0`:

- GCC 13 and Clang 18 Debug/Release, separate Clang 18 ASan and TSan: **six
  configurations x three transports = 18 passing runs**, all 9 PostgreSQL cases
  enabled. Each run covers `disable`, `require` or `verify-full`, with actual
  transport checked through `pg_stat_ssl`. The verified-TLS example also passes.
- Prepared-query checks cover statement reuse with fresh values, text/binary OID
  separation, NULLs, no-parameter queries, both preparation and execution errors,
  the 128-entry bound, reconnection, large owned buffers, overlapping calls and
  handle destruction/cancellation while a server-side advisory lock proves the
  query is suspended. Independent contexts run on two barrier-synchronized
  threads. PostgreSQL async tests have no exclusions.
- All 5 pool regressions pass under independently built Clang 18 ASan and TSan,
  with both 1 worker and 4 workers sharing one executor. A compile of the
  affected regressions against the exact `e9719da` pool header fails: reconnects
  lose both capacity slots, and a new checkout overtakes an existing waiter.
  Backend gates, completion channels and an executor barrier establish the
  interleavings; sleeps are not used to establish correctness.
- The optional probe regression verifies that the default still calls ping,
  opting out skips SQL and a disconnected slot is reconnected. Real database
  consumers below also cover peer termination with buffered TLS close data.
- The CI matrix now runs the shared-executor pool regressions with 4 workers in
  every GCC/Clang and sanitizer configuration. New-head CI is recorded separately
  from these completed local checks; a prior successful head is not evidence for
  the update.

The current local server is PostgreSQL 18.7 on port 55418; libpq is 16.15,
standalone Asio is 1.36.0. The test account is the public Arena `bench` account in
database `benchmark`. Exact commands and compiler/CMake flags are retained in
`/tmp/ormpp-pg-async-validation/pr-prepared-update/{commands,configurations}.json`.
For example:

```sh
cmake --build /tmp/ormpp-pg-async-validation/build-clang-asan \
  --target test_postgresql_async -j1
ORMPP_ASYNC_PG_HOST=localhost ORMPP_ASYNC_PG_PORT=55418 \
  ORMPP_ASYNC_PG_USER=bench ORMPP_ASYNC_PG_PASSWORD=bench \
  ORMPP_ASYNC_PG_DB=benchmark PGSSLMODE=verify-full \
  PGSSLROOTCERT=/data/code/cinatra/build-httparena-review/postgresql-cases/pg-server.crt \
  ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
  /tmp/ormpp-pg-async-validation/build-clang-asan/tests/test_postgresql_async
ORMPP_POOL_TEST_THREADS=4 TSAN_OPTIONS=halt_on_error=1 \
  setarch x86_64 -R \
  /tmp/ormpp-pg-async-validation/pr-prepared-update/pool-tsan
```

Use the separate `build-clang-tsan` executable with `setarch x86_64 -R` and
`TSAN_OPTIONS=halt_on_error=1` for TSan; do not combine ASan and TSan. Repeat the
database run with `PGSSLMODE=disable` and `require`. The unsanitized GCC/Clang
Debug and Release directories are listed in `configurations.json`.

One GCC 13 test assertion originally combined inspection of an error with
`co_await ping()`, which clears diagnostics. An isolated reproducer produced
different combined-expression results with GCC 13 and Clang 18. The final test
observes the error first, then awaits and checks ping separately; all final
compiler and sanitizer runs pass. This test change does not modify the driver.

### Local HttpArena consumer

A local cinatra consumer, based on `389776e05c7a25f9fbbc367ab607cd7cf6b0ae5e`
plus its local PostgreSQL cases, exercises this exact ormpp implementation.
Those cinatra changes are outside this PR. Separate Clang 17 ASan and TSan
consumer builds pass six checks: plain database transport and verified TLS,
including both HTTP TLS response-buffer modes. Checks include concurrent cold
initialization, fresh database edits, peer termination/reconnect, connection
budgets and shutdown while an observed database lock holds a query suspended.

The prepared-cache A/B uses the same GCC 14.2 Release/O3/LTO/x86-64-v3 binary,
switching only `CINATRA_PG_PREPARED=0/1`. The SQL ping stays off in both variants.
Server CPUs 0,1 run two HTTP workers, each with its own single-thread context;
two generator threads share CPU 2, and PostgreSQL uses CPU 3. Both cases use
1024 HTTP connections, a total 256-connection database pool, pipeline 1, three
10-second samples with 3-second warmups, and alternating implementation order.
`async-db` rotates limits 5/10/20/35/50 with min=10, max=50 and 25 requests per
connection. The dataset has 100000 items without a price index and 200 fortunes.

| Case | Cache off RPS (three runs) | Cache on RPS (three runs) | On/off median gain | Dusty RPS (three runs) |
| --- | --- | --- | --- | --- |
| async-db | 6955.43 / 6799.24 / 6797.44 | 8101.10 / 7975.97 / 8050.33 | 18.4% | 7922.31 / 8025.59 / 7710.70 |
| fortunes | 9371.22 / 9176.96 / 9565.13 | 10437.16 / 10628.15 / 10572.34 | 12.8% | 10984.04 / 10757.73 / 10911.97 |

All 18 samples have only 2xx responses and no generator connect/read/timeout
errors. Response body hashes and complete HTTP lengths match across all three
variants for the five limits and fortunes. The corrected generator binary SHA256
is `0687ed2a08a0c916eb8fbdc6b8e03e153869bdd75a9afe354ce2db29b91a3849`.
Manifest, image IDs, commands, raw results and byte audits are retained under
`/data/code/cinatra/build-httparena-review/postgresql-cases/prepared-cache-20261117-215659`.

These are local four-logical-CPU results with PostgreSQL near one CPU of usage,
not an official Arena score or a 64-logical-CPU scalability result. Full local
synchronous/MySQL/platform suites were not rerun for this update; their latest
CI results must be checked after publishing. The known synchronous SQLite TSan
exclusion below remains separate from PostgreSQL and pool sanitizer results.
The user-owned `tests/benchmark_adapter.cpp` is excluded from the PR.

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
performed by the backend. The generic pool retains its existing strand while
adding the recovery/waiter changes above. `mysql_async` itself is unchanged.

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

## Earlier local results for the single-thread correction

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
