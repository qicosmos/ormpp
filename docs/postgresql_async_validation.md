# PostgreSQL async validation record

Local validation date: 2026-11-17. Base: `c5a63f5bb5ccae8740754d4c744815876ff7656f`.
Tested C++ implementation: `5ed42b6a583af1804d7cca40708c054ceb295677`.
The driver C++ sources remain unchanged in follow-ups. CI also required adding
`<coroutine>` to the shared traits header for GCC 15; this include-only change
was rebuilt and rechecked under ASan and TSan.
GitHub Actions results are separate from the local results below; see the PR checks
for the latest published commit.

## Environment and configurations

Ubuntu 24.04, GCC 13.3, Clang 18.1.3, standalone Asio 1.36.0,
libpq/PostgreSQL 16.15. A dedicated PostgreSQL server listened on
`127.0.0.1:55432`; TLS used a local CA certificate valid for `localhost` and
`127.0.0.1`. Authentication over TCP used a password.

Every row below built and ran the complete `test_postgresql_async` executable
(5 cases, including separate 1-worker and 4-worker runs). Each configuration
passed independently with `PGSSLMODE=disable`, `require`, and `verify-full`, for
18 successful configuration/transport combinations. The tests also inspect
`pg_stat_ssl` to check the actual transport.

| Build directory suffix | Compiler / build | Sanitizer | Synchronous PG enabled |
| --- | --- | --- | --- |
| gcc-release | GCC / Release | off | no |
| gcc-debug | GCC / Debug | off | no |
| clang-release | Clang / Release | off | yes |
| clang-debug | Clang / Debug | off | no |
| clang-asan | Clang / Debug | AddressSanitizer | yes |
| clang-tsan | Clang / Debug | ThreadSanitizer | yes |

ASan and TSan were configured and run separately. TSan executed the instrumented
binary successfully; the ASan results are not used as evidence of race freedom.

Additional local results:

- Existing SQLite suite, GCC Release: 63 cases / 482 assertions passed.
- Existing SQLite + synchronous PostgreSQL suite, Clang ASan: 64 cases /
  799 assertions passed, with no exclusions.
- Existing SQLite + synchronous PostgreSQL suite, Clang TSan: the initial
  **unfiltered run failed** in the unchanged SQLite diagnostic state. After
  excluding exactly the test described below, 63 cases / 796 assertions passed,
  with 1 case excluded. This is not a full-suite TSan pass.
- The new example built and ran successfully with `verify-full`.
- Both async backend headers compiled together with both feature macros defined.
- `git-clang-format-18 --diff HEAD` and `git diff --check` passed before publication.

Local integration runs did not enable MySQL, MariaDB, SQLCipher, Windows or macOS.
The new PostgreSQL workflow runs on Linux. Existing platform/backend workflows
remain enabled. Existing synchronous tests were not built in every local matrix
entry; the explicit results above identify the configurations actually checked.

## Commands

Artifacts and exact command/result JSON are retained locally in
`/tmp/ormpp-pg-async-validation`, including `final-matrix.json`, build/configure
logs, and `final-test-<configuration>-<sslmode>.log`.

All configurations used the following common arguments (replace the suffix and
compiler/sanitizer flags according to the table):

```sh
cmake -S /data/code/ormpp -B /tmp/ormpp-pg-async-validation/build-clang-asan \
  -DCMAKE_C_COMPILER=clang-18 -DCMAKE_CXX_COMPILER=clang++-18 \
  -DCMAKE_BUILD_TYPE=Debug -DENABLE_PG_ASYNC=ON -DENABLE_PG=ON \
  -DORMPP_ASIO_INCLUDE_DIR=/data/code/cinatra/include -DBUILD_EXAMPLES=OFF \
  -DENABLE_SANITIZER=ON -DENABLE_THREAD_SANITIZER=OFF
cmake --build /tmp/ormpp-pg-async-validation/build-clang-asan \
  --target test_postgresql_async test_ormpp -j2
ORMPP_ASYNC_PG_HOST=localhost ORMPP_ASYNC_PG_PORT=55432 PGPORT=55432 \
  PGSSLMODE=verify-full PGSSLROOTCERT=/tmp/ormpp-pg-async-data/server.crt \
  ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 TSAN_OPTIONS=halt_on_error=1 \
  ctest --test-dir /tmp/ormpp-pg-async-validation/build-clang-asan \
  -R '^test_postgresql_async$' -V
```

For TSan, use `-DENABLE_SANITIZER=OFF -DENABLE_THREAD_SANITIZER=ON` in a separate
build directory. For unsanitized builds, set both options OFF. Repeat the test
command for `disable` and `require`. The test credentials default to the dedicated
`root` / `123456` / `test_ormppdb` setup and can be overridden through
`ORMPP_ASYNC_PG_USER`, `ORMPP_ASYNC_PG_PASSWORD`, and `ORMPP_ASYNC_PG_DB`.

Existing-suite checks use `ctest -R '^test_ormpp$' -V`, with `PGPORT=55432` and
`PGSSLMODE=disable`. The filtered TSan command is given below. GCC configurations
use `cc`/`c++`; Release configurations use `-DCMAKE_BUILD_TYPE=Release`.
The example build enabled `BUILD_EXAMPLES=ON` and ran
`build-clang-release/example/ormpp_pg_async_example` with `PGHOST=localhost`,
`PGPORT=55432`, `PGUSER=root`, `PGDATABASE=test_ormppdb`, password authentication,
`PGSSLMODE=verify-full`, and the same `PGSSLROOTCERT`.

## Baseline and deterministic interleavings

At base `c5a63f5`, the feature probe including `ormpp/postgresql_async.hpp` fails
because that backend does not exist. The same probe compiles on the implementation
commit. This establishes feature absence, not a claim of an old runtime regression.
A separate reflected `char name[8]` DDL probe fails to compile on the base and
passes on the implementation, confirming the character-array mapping correction.

The runtime concurrency tests use three real database connections: a blocker owns
an advisory lock, the target requests that lock, and an observer queries
`pg_locks` until it sees the target PID waiting on an ungranted lock. Only then do
tests queue a disconnect and destroy the handle, or issue cancellation. The
blocker releases the lock explicitly. The tests check completion results, closed
connection behavior and reconnection, on both one and four `io_context` workers.
Sleeps and repeated successful runs are not used to establish these interleavings.
Large temporary parameters are destroyed before their lazy operation is awaited.

The lifetime/executor audit checked that input buffers and the operation closure
are owned across suspension, each operation retains connection state, libpq and
socket readiness operations run on the connection strand, borrowed socket wrappers
release the descriptor before libpq calls/close, and cancellation closes an
undrained connection before subsequent reuse. Asio DNS resolution resumes on that
same strand before libpq receives `hostaddr`.

## Existing SQLite TSan exclusion

The test `issue #238: multi-thread concurrent first query` races while its workers
initialize independent SQLite connections: `sqlite::reset_error()` writes the
shared static `has_error_` (and diagnostic string). No async PostgreSQL code is
present in that test or in the baseline reproduction.

The unchanged original test was built and run under Clang TSan from an archive of
base `c5a63f5`, using `ENABLE_PG=ON`, `ENABLE_SANITIZER=OFF`, and
`ENABLE_THREAD_SANITIZER=ON`. It failed with exit 66 and the same report:

```text
WARNING: ThreadSanitizer: data race
ormpp::sqlite::reset_error() .../baseline/ormpp/sqlite.hpp:26:16
ormpp::sqlite::connect(...) .../baseline/ormpp/sqlite.hpp:41:5
Location is global 'ormpp::sqlite::has_error_'
```

A second baseline probe uses `std::barrier(2)` immediately before two calls to
`sqlite::reset_error()` and reproduces the same race without a database or sleeps.
Baseline logs: `baseline-existing-clang-tsan-238.log` and
`baseline-sqlite-error-race.log`. The complete unfiltered final-suite failure is
retained as `final-existing-clang-tsan.log`.

To keep the new CI job scoped without hiding this existing failure, only this
exact synchronous SQLite case is excluded in its TSan step:

```sh
PGPORT=55432 PGSSLMODE=disable TSAN_OPTIONS=halt_on_error=1 \
  /tmp/ormpp-pg-async-validation/build-clang-tsan/tests/test_ormpp \
  '--test-case-exclude=issue #238: multi-thread concurrent first query'
```

The PostgreSQL async TSan executable has no exclusions. ASan and the other
configurations continue to run the synchronous suite without this exclusion.

## First published CI and corrections

At head `47ddbc8`, GitHub's Ubuntu 26.04 / GCC 15.2 runner diagnosed the missing
`<coroutine>` include in shared async traits while parsing the query-builder
coroutine, including synchronous configurations. The failure is recorded in
[job 133714325856](https://github.com/qicosmos/ormpp/actions/runs/43986043833/job/133714325856).
The shared header now includes its required standard dependency directly.

The initial TSan workflow also exposed YAML plain-scalar parsing of the ` #238`
text in its test filter. The step now uses a block scalar; every parsed `run`
script was checked with `bash -n`. Neither failure is a sanitizer report from
the PostgreSQL backend. Replacement CI checks on the updated head are tracked
on the PR.

The coverage runner additionally failed before configuration with MySQL error
1698: its setup action used `sudo mysql` and left root's Unix-socket
authentication enabled, while verification/tests connected as an ordinary user.
The coverage workflow now explicitly configures its temporary root test account
with `caching_sha2_password` and an empty test password, matching the existing
test configuration. This change affects only the CI database. The original
failure is recorded in [job 133714325429](https://github.com/qicosmos/ormpp/actions/runs/43986043705/job/133714325429).
