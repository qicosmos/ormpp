#ifdef ORMPP_ENABLE_PG_ASYNC

#include <asio.hpp>
#include <asio/experimental/concurrent_channel.hpp>
#include <atomic>
#include <cstdlib>
#include <future>
#include <limits>
#include <thread>

#include "async_connection_pool.hpp"
#include "dbng.hpp"
#include "doctest.h"
#include "postgresql_async.hpp"

using namespace ormpp;

struct pg_async_person {
  int id;
  std::string name;
  int age;
};
YLT_REFL(pg_async_person, id, name, age)
REGISTER_AUTO_KEY(pg_async_person, id)

struct pg_async_composite {
  int first;
  int second;
  std::string name;
  int age;
};
YLT_REFL(pg_async_composite, first, second, name, age)
REGISTER_CONFLICT_KEY(pg_async_composite, second, first)

struct pg_async_defaults {
  std::int64_t id;
  std::string generated;
};
YLT_REFL(pg_async_defaults, id, generated)
REGISTER_AUTO_KEY(pg_async_defaults, id)
REGISTER_SKIP_INSERT_FIELD(pg_async_defaults, generated)

struct pg_async_mixed {
  int recordId;
  std::string displayName;
};
REGISTER_AUTO_KEY(pg_async_mixed, recordId)
YLT_REFL(pg_async_mixed, recordId, displayName)

enum class pg_async_color { blue = 7 };
struct pg_async_types {
  int id;
  std::optional<std::string> name;
  bool enabled;
  pg_async_color color;
  std::int64_t big;
  double precise;
  float real;
  char letter;
  std::array<char, 8> fixed;
  char c_array[8];
  blob bytes;
  ormpp::date day;
  decimal<12, 2> amount;
};
YLT_REFL(pg_async_types, id, name, enabled, color, big, precise, real, letter,
         fixed, c_array, bytes, day, amount)
REGISTER_AUTO_KEY(pg_async_types, id)

namespace {
void require(bool condition, const std::string& message) {
  if (!condition)
    throw std::runtime_error(message);
}
std::string env(const char* name, const char* fallback) {
  auto value = std::getenv(name);
  return value ? value : fallback;
}

auto config() {
  return std::tuple{
      env("ORMPP_ASYNC_PG_HOST", "127.0.0.1"),
      env("ORMPP_ASYNC_PG_USER", "root"),
      env("ORMPP_ASYNC_PG_PASSWORD", "123456"),
      env("ORMPP_ASYNC_PG_DB", "test_ormppdb"),
      std::optional<int>{5},
      std::optional<int>{std::stoi(env("ORMPP_ASYNC_PG_PORT", "5432"))}};
}

asio::awaitable<void> connect(postgresql_async& db) {
  bool connected = co_await db.connect(config());
  require(connected, "connect: " + db.get_last_error());
}

template <typename T = std::int64_t>
asio::awaitable<T> scalar(postgresql_async& db, std::string sql) {
  auto rows = co_await db.query_s<std::tuple<T>>(sql);
  require(rows.size() == 1, "scalar: " + db.get_last_error() + " SQL: " + sql);
  co_return std::get<0>(rows.front());
}

void run(asio::awaitable<void> operation, asio::io_context& ctx,
         int threads = 1) {
  auto future = asio::co_spawn(asio::make_strand(ctx), std::move(operation),
                               asio::use_future);
  std::vector<std::thread> workers;
  for (int n = 1; n < threads; ++n)
    workers.emplace_back([&] {
      ctx.run();
    });
  ctx.run();
  for (auto& worker : workers) worker.join();
  future.get();
}

asio::awaitable<void> crud() {
  auto executor = co_await asio::this_coro::executor;
  dbng<postgresql_async> db(executor);
  require(co_await db.connect(config()), "dbng connect");
  require(co_await db.ping(), "ping");
  auto ssl = co_await db.query_s<std::tuple<bool>>(
      "SELECT ssl FROM pg_stat_ssl WHERE pid=pg_backend_pid()");
  require(ssl.size() == 1, "inspect actual TLS session");
  if (env("PGSSLMODE", "prefer") == "disable") {
    require(!std::get<0>(ssl[0]), "plaintext session expected");
  }
  else if (env("PGSSLMODE", "prefer") == "require" ||
           env("PGSSLMODE", "prefer") == "verify-full") {
    require(std::get<0>(ssl[0]), "TLS session expected");
  }
  require(co_await db.execute("DROP TABLE IF EXISTS pg_async_person"), "drop");
  require(co_await db.create_datatable<pg_async_person>(
              ormpp_not_null{{"name"}}, ormpp_unique{{"name"}}),
          "create: " + db.get_last_error());
  auto pending_insert = db.insert(pg_async_person{0, "alice", 18});
  require(co_await std::move(pending_insert) == 1,
          "insert: " + db.get_last_error());
  auto id =
      co_await db.get_insert_id_after_insert(pg_async_person{0, "bob", 20});
  require(id > 0, "returning: " + db.get_last_error());
  std::vector<pg_async_person> batch{{0, "c", 30}, {0, "d", 40}};
  require(co_await db.insert(batch) == 2 && db.get_last_affect_rows() == 2,
          "batch insert");
  require(co_await db.replace(
              pg_async_person{static_cast<int>(id), "bob2", 21}) == 1,
          "replace: " + db.get_last_error());
  require(co_await db.update(
              pg_async_person{static_cast<int>(id), "bob3", 22}) == 1,
          "update: " + db.get_last_error());
  require(
      co_await db.update_some<&pg_async_person::age, &pg_async_person::name>(
          pg_async_person{static_cast<int>(id), "bob4", 23}) == 1,
      "partial update: " + db.get_last_error());
  auto selected = co_await db.query_s<pg_async_person>("id=$1", id);
  require(selected.size() == 1 && selected.front().name == "bob4" &&
              selected.front().age == 23,
          "read updated row");
  auto builder_rows = co_await db.select(all)
                          .from<pg_async_person>()
                          .where(col(&pg_async_person::id).param())
                          .collect(id);
  require(builder_rows.size() == 1 && builder_rows.front().name == "bob4",
          "builder select");
  require(co_await db.select(col(&pg_async_person::age))
                  .from<pg_async_person>()
                  .where(col(&pg_async_person::id).param())
                  .scalar(id) == 23,
          "builder scalar");
  require(co_await db.update<pg_async_person>()
                  .set(col(&pg_async_person::age), 24)
                  .where(col(&pg_async_person::id) == id)
                  .execute() == 1,
          "builder update");
  auto joined = co_await db.query_s<std::tuple<pg_async_person, std::string>>(
      "SELECT p.*, 'joined' FROM pg_async_person p WHERE id=$1", id);
  require(joined.size() == 1 && std::get<0>(joined[0]).age == 24 &&
              std::get<1>(joined[0]) == "joined",
          "nested row mapping");
  require(co_await db.begin(), "begin");
  require(co_await db.insert(pg_async_person{0, "rollback", 1}) == 1,
          "tx insert");
  require(co_await db.rollback(), "rollback");
  require((co_await db.query_s<pg_async_person>("name=$1", "rollback")).empty(),
          "rollback row");
  require(co_await db.begin(), "begin commit");
  require(co_await db.insert(pg_async_person{0, "commit", 2}) == 1,
          "tx insert");
  require(co_await db.commit(), "commit");
  std::vector<pg_async_person> duplicate{{0, "must_rollback", 1},
                                         {0, "alice", 2}};
  require(co_await db.insert(duplicate) == std::numeric_limits<int>::min(),
          "duplicate batch must fail");
  require(db.get_last_error().find("duplicate key") != std::string::npos,
          "preserve rollback error");
  require((co_await db.query_s<pg_async_person>("name=$1", "must_rollback"))
              .empty(),
          "atomic batch rollback");
  require(co_await db.remove<pg_async_person>()
                  .where(col(&pg_async_person::id) == id)
                  .execute() == 1,
          "builder delete");
  require(
      co_await db.delete_records_s<pg_async_person>("name=$1", "alice") == 1,
      "delete params");

  require(co_await db.execute("DROP TABLE IF EXISTS pg_async_composite"),
          "drop composite");
  require(co_await db.create_datatable<pg_async_composite>(),
          "create composite");
  pg_async_composite composite{11, 22, "old", 1};
  require(co_await db.insert(composite) == 1, "insert composite");
  composite.name = "new";
  composite.age = 99;
  require(co_await db.update_some<&pg_async_composite::age,
                                  &pg_async_composite::name>(composite) == 1,
          "reversed key/partial-member order");
  auto composites = co_await db.query_s<pg_async_composite>();
  require(composites.size() == 1 && composites[0].name == "new" &&
              composites[0].age == 99,
          "composite values");
  require(co_await db.replace(composite) == 1, "composite replace");

  require(co_await db.execute("DROP TABLE IF EXISTS pg_async_defaults"),
          "drop defaults");
  require(co_await db.execute("CREATE TABLE pg_async_defaults(id bigserial "
                              "PRIMARY KEY, generated text DEFAULT 'server')"),
          "create defaults");
  auto default_id = co_await db.get_insert_id_after_insert(pg_async_defaults{});
  require(default_id > 0, "default-only RETURNING");
  auto defaults = co_await db.query_s<pg_async_defaults>();
  require(defaults.size() == 1 && defaults.front().generated == "server",
          "skip insert fields");
  std::vector<pg_async_defaults> default_batch(2);
  auto last_id = co_await db.get_insert_id_after_insert(default_batch);
  require(last_id > default_id && db.get_last_affect_rows() == 2,
          "batch RETURNING");

  require(co_await db.execute("DROP TABLE IF EXISTS pg_async_mixed"),
          "drop mixed case");
  require(co_await db.create_datatable<pg_async_mixed>(), "create mixed case");
  auto mixed_id =
      co_await db.get_insert_id_after_insert(pg_async_mixed{0, "case folding"});
  require(mixed_id > 0, "mixed-case RETURNING");
  auto mixed_rows = co_await db.select(all)
                        .from<pg_async_mixed>()
                        .where(col(&pg_async_mixed::recordId).param())
                        .collect(mixed_id);
  require(mixed_rows.size() == 1 && mixed_rows[0].displayName == "case folding",
          "mixed-case builder");

  require(co_await db.execute("DROP TABLE IF EXISTS pg_async_types"),
          "drop types");
  require(co_await db.create_datatable<pg_async_types>(),
          "create types: " + db.get_last_error());
  pg_async_types value{0,
                       "quotes ' $1 ? \\ ; --",
                       true,
                       pg_async_color::blue,
                       INT64_C(9007199254740993),
                       1.23456789012345,
                       0.1234567f,
                       'x',
                       {'a', 'b', 'c'},
                       "fixed",
                       {'a', '\0', '\xff'},
                       {"2026-11-17"},
                       {"123456.78"}};
  require(co_await db.insert(value) == 1,
          "insert types: " + db.get_last_error());
  auto values = co_await db.query_s<pg_async_types>("name=$1", *value.name);
  require(values.size() == 1, "parameter injection string");
  const auto& roundtrip = values.front();
  require(roundtrip.name == value.name && roundtrip.enabled &&
              roundtrip.color == value.color && roundtrip.big == value.big &&
              roundtrip.precise == value.precise &&
              roundtrip.real == value.real && roundtrip.letter == 'x' &&
              roundtrip.fixed == value.fixed &&
              std::string(roundtrip.c_array) == "fixed" &&
              roundtrip.bytes == value.bytes &&
              roundtrip.day.value == "2026-11-17" &&
              roundtrip.amount.value == "123456.78",
          "type roundtrip");
  value.name.reset();
  value.enabled = false;
  require(co_await db.insert(value) == 1, "NULL insert");
  auto nulls = co_await db.query_s<pg_async_types>("name IS NULL");
  require(nulls.size() == 1 && !nulls[0].name && !nulls[0].enabled,
          "NULL/bool mapping");
  auto booleans =
      co_await db.query_s<std::tuple<bool, bool, std::optional<int>>>(
          "SELECT true, false, $1::int", std::nullopt);
  require(booleans.size() == 1 && std::get<0>(booleans[0]) &&
              !std::get<1>(booleans[0]) && !std::get<2>(booleans[0]),
          "native boolean result");
  auto views =
      co_await db.query_s<std::tuple<std::string_view, std::string_view>>(
          "SELECT repeat('x', 200), 'tail' FROM generate_series(1, 100)");
  require(views.size() == 100 &&
              std::get<0>(views.front()) == std::string(200, 'x') &&
              std::get<1>(views.back()) == "tail",
          "string_view owned storage");
  require(!(co_await db.execute("SELECT 1; SELECT missing_column; SELECT 2")),
          "SQL error");
  require(db.has_error(), "record SQL error");
  require(co_await db.ping() && !db.has_error(), "drain SQL errors and reuse");
  require((co_await db.query_s<std::tuple<int>>("SELECT 1, 2")).empty() &&
              db.has_error(),
          "column mismatch");
  require(co_await db.ping(), "reuse after mapping error");
  auto invalid_text = co_await db.query_s<std::tuple<std::string>>(
      "SELECT $1::text", std::string("a\0b", 3));
  require(invalid_text.empty() && db.has_error() &&
              db.get_last_error().find("NUL") != std::string::npos,
          "reject NUL text");
  require(co_await db.ping(), "reuse after invalid parameter");
  require(co_await db.insert(std::vector<pg_async_person>{}) == 0,
          "empty batch");
  require(co_await db.execute("DROP TABLE pg_async_person"), "drop builder");
  require(co_await db.create_table<pg_async_person>()
              .auto_increment(col(&pg_async_person::id))
              .not_null(col(&pg_async_person::name))
              .execute(),
          "create builder");
  require(co_await db.alter_table<pg_async_person>()
              .add_column("nickname", "TEXT")
              .execute(),
          "alter builder");
  require(co_await db.disconnect(), "disconnect");
  require(!(co_await db.ping()), "ping disconnected");
  require(co_await db.connect(config()), "reconnect");
  require(!(co_await db.execute("COPY (SELECT 1) TO STDOUT")),
          "unsupported COPY closes connection");
  require(!(co_await db.ping()), "COPY invalidates connection");
  require(co_await db.connect(config()) && co_await db.disconnect(),
          "reconnect after COPY");
}

// Observe the server-side wait before releasing the lock. This proves that the
// target operation suspended, even with one io_context worker; no timing
// sleeps.
asio::awaitable<void> wait_for_lock(postgresql_async& observer, int pid) {
  auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
  while (std::chrono::steady_clock::now() < deadline) {
    auto waiting = co_await observer.query_s<std::tuple<bool>>(
        "SELECT EXISTS(SELECT FROM pg_locks WHERE pid=$1 AND "
        "locktype='advisory' AND NOT granted)",
        pid);
    require(waiting.size() == 1, "observe lock");
    if (std::get<0>(waiting[0]))
      co_return;
  }
  throw std::runtime_error("target did not reach advisory lock wait");
}

asio::awaitable<void> concurrency(asio::any_io_executor io_executor) {
  auto ex = co_await asio::this_coro::executor;
  postgresql_async blocker(io_executor), observer(io_executor);
  co_await connect(blocker);
  co_await connect(observer);
  using completion_queue =
      asio::experimental::concurrent_channel<void(asio::error_code, int)>;
  completion_queue done(ex, 8);
  std::atomic<int> sequence{0};
  auto completion = [&](int expected) {
    return [&, expected](std::exception_ptr error, bool result) {
      bool valid = !error && result && ++sequence == expected;
      done.try_send(asio::error_code{}, valid ? expected : -expected);
    };
  };
  auto target = std::make_unique<postgresql_async>(io_executor);
  co_await connect(*target);
  const int pid = co_await scalar<int>(*target, "SELECT pg_backend_pid()");
  require(co_await blocker.execute("SELECT pg_advisory_lock(8530242)"),
          "take lock");
  asio::co_spawn(ex, target->execute("SELECT pg_advisory_lock(8530242)"),
                 completion(1));
  co_await wait_for_lock(observer, pid);
  // Queue disconnect while a query owns the socket, then drop the public
  // handle. Both lazy operations must retain state and close only after the
  // query drains.
  asio::co_spawn(ex, target->disconnect(), completion(2));
  target.reset();
  require(co_await blocker.execute("SELECT pg_advisory_unlock(8530242)"),
          "release lock");
  auto first = co_await done.async_receive(asio::use_awaitable);
  auto second = co_await done.async_receive(asio::use_awaitable);
  require(first == 1 && second == 2,
          "serialized disconnect and handle lifetime");
  // Reuse the descriptor after destruction and execute a large parameter while
  // temporary inputs are already gone before the first suspension.
  postgresql_async owner(io_executor);
  co_await connect(owner);
  auto owned = owner.query_s<std::tuple<std::string>>(
      "SELECT $1::text", std::string(2 * 1024 * 1024, 'z'));
  auto data = co_await std::move(owned);
  require(data.size() == 1 &&
              std::get<0>(data[0]) == std::string(2 * 1024 * 1024, 'z'),
          "owned write buffers");
  require(co_await owner.disconnect(), "owner disconnect");
  require(co_await observer.disconnect() && co_await blocker.disconnect(),
          "helper disconnect");
}

asio::awaitable<void> cancellation(asio::any_io_executor io_executor) {
  auto ex = co_await asio::this_coro::executor;
  postgresql_async blocker(io_executor), observer(io_executor),
      target(io_executor);
  co_await connect(blocker);
  co_await connect(observer);
  co_await connect(target);
  int pid = co_await scalar<int>(target, "SELECT pg_backend_pid()");
  require(co_await blocker.execute("SELECT pg_advisory_lock(8530243)"),
          "cancel blocker lock");
  asio::experimental::concurrent_channel<void(asio::error_code, bool)> done(ex,
                                                                            1);
  asio::cancellation_signal signal;
  asio::co_spawn(ex, target.execute("SELECT pg_advisory_lock(8530243)"),
                 asio::bind_cancellation_slot(
                     signal.slot(), [&](std::exception_ptr error, bool ok) {
                       done.try_send(asio::error_code{}, !error && !ok);
                     }));
  co_await wait_for_lock(observer, pid);
  co_await asio::co_spawn(
      target.get_executor(),
      [&]() -> asio::awaitable<void> {
        signal.emit(asio::cancellation_type::terminal);
        co_return;
      },
      asio::use_awaitable);
  require(co_await done.async_receive(asio::use_awaitable),
          "cancelled query failure");
  require(!(co_await target.ping()), "cancel closes undrained socket");
  require(co_await target.connect(config()) && co_await target.ping(),
          "reconnect cancelled connection");
  require(co_await blocker.execute("SELECT pg_advisory_unlock(8530243)"),
          "cancel blocker unlock");
  require(co_await target.disconnect() && co_await observer.disconnect() &&
              co_await blocker.disconnect(),
          "cancel cleanup");
}

asio::awaitable<void> pool() {
  auto ex = co_await asio::this_coro::executor;
  pool_options options;
  options.log_pool_exhaustion = false;
  auto connections =
      std::make_shared<async_connection_pool<postgresql_async>>(ex, options);
  auto cfg = config();
  require(co_await std::apply(
              [&](const auto&... args) {
                return connections->init(1, args...);
              },
              cfg),
          "pool init");
  auto lease = co_await connections->get();
  require(lease && co_await lease->ping(), "pool lease");
  lease.reset();
  auto reused = co_await connections->get();
  require(reused && co_await reused->ping(), "pool reuse");
  reused.reset();
  co_await connections->close_all();
  require(!(co_await connections->get()), "closed pool");
}

asio::awaitable<void> connect_failures() {
  auto ex = co_await asio::this_coro::executor;
  postgresql_async db(ex);
  require(!(co_await db.connect("")) && db.has_error(),
          "reject implicit blocking DNS");
  auto cfg = config();
  std::get<2>(cfg) = "invalid_password";
  require(!(co_await db.connect(cfg)) && db.has_error(),
          "reject wrong password");
  co_await connect(db);
  require(co_await db.disconnect(), "disconnect after auth failure");
  // A local TCP listener accepts but never replies to SSL/startup. The accept
  // completion is an explicit rendezvous, and the client's own timeout ends it.
  asio::ip::tcp::acceptor acceptor(ex, {asio::ip::tcp::v4(), 0});
  asio::ip::tcp::socket peer(ex);
  auto accepted =
      asio::co_spawn(ex, acceptor.async_accept(peer, asio::use_awaitable),
                     asio::use_awaitable);
  const auto start = std::chrono::steady_clock::now();
  auto attempt = asio::co_spawn(ex,
                                db.connect("127.0.0.1", "root", "", "test", 1,
                                           acceptor.local_endpoint().port()),
                                asio::use_awaitable);
  // Start connect before awaiting accept, using a completion queue.
  asio::experimental::concurrent_channel<void(asio::error_code, bool)> done(ex,
                                                                            1);
  asio::co_spawn(ex, std::move(attempt),
                 [&](std::exception_ptr error, bool ok) {
                   done.try_send(asio::error_code{}, !error && !ok);
                 });
  co_await std::move(accepted);
  require(co_await done.async_receive(asio::use_awaitable),
          "stalled connect must time out");
  require(db.get_last_error().find("timed out") != std::string::npos,
          "timeout error");
  require(std::chrono::steady_clock::now() - start < std::chrono::seconds(4),
          "bounded timeout");
  peer.close();
  acceptor.close();
  co_await connect(db);
  require(co_await db.ping() && co_await db.disconnect(),
          "reconnect after timeout");
}
}  // namespace

TEST_CASE("postgresql async CRUD and builders") {
  asio::io_context ctx;
  CHECK_NOTHROW(run(crud(), ctx));
}
TEST_CASE("postgresql async shared executor and lifetime") {
  for (int threads : {1, 4}) {
    INFO("io_context workers: " << threads);
    asio::io_context ctx;
    CHECK_NOTHROW(run(concurrency(ctx.get_executor()), ctx, threads));
  }
}
TEST_CASE("postgresql async cancellation") {
  for (int threads : {1, 4}) {
    INFO("io_context workers: " << threads);
    asio::io_context ctx;
    CHECK_NOTHROW(run(cancellation(ctx.get_executor()), ctx, threads));
  }
}
TEST_CASE("postgresql async pool") {
  asio::io_context ctx;
  CHECK_NOTHROW(run(pool(), ctx));
}
TEST_CASE("postgresql async connect failures and timeout") {
  asio::io_context ctx;
  CHECK_NOTHROW(run(connect_failures(), ctx));
}
#endif
