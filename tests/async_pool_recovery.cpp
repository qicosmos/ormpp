#if defined(ORMPP_ENABLE_PG_ASYNC) || defined(ORMPP_ENABLE_MYSQL_ASYNC)

#include <asio.hpp>
#include <asio/experimental/concurrent_channel.hpp>
#include <cstdlib>
#include <future>
#include <sstream>
#include <thread>

#include "async_connection_pool.hpp"
#include "doctest.h"

namespace pool_recovery_test {
using event = asio::experimental::concurrent_channel<void(asio::error_code)>;

void run(asio::io_context &context, std::future<void> &done) {
  const auto *setting = std::getenv("ORMPP_POOL_TEST_THREADS");
  const auto count = setting ? std::stoi(setting) : 1;
  std::vector<std::thread> threads;
  for (int i = 0; i < count; ++i) {
    threads.emplace_back([&] {
      context.run();
    });
  }
  for (auto &thread : threads) {
    thread.join();
  }
  done.get();
}

struct exhaustion_log : std::stringbuf {
  explicit exhaustion_log(event &observed)
      : observed(observed), original(std::cerr.rdbuf(this)) {}
  ~exhaustion_log() { std::cerr.rdbuf(original); }
  int sync() override {
    if (str().find("Pool exhausted") != std::string::npos) {
      observed.try_send(asio::error_code{});
    }
    str("");
    return 0;
  }
  event &observed;
  std::streambuf *original;
};

struct database {
  inline static bool online = true;
  inline static event *connecting = nullptr;
  inline static event *resume_connect = nullptr;
  inline static std::atomic<int> ping_calls = 0;
  bool connected = false;

  explicit database(asio::any_io_executor) {}
  asio::awaitable<bool> connect(const std::string &, const std::string &,
                                const std::string &, const std::string &,
                                std::optional<int>, std::optional<int>) {
    if (!online && connecting) {
      connecting->try_send(asio::error_code{});
      co_await resume_connect->async_receive(asio::use_awaitable);
    }
    connected = online;
    co_return connected;
  }
  bool is_open() const { return connected && online; }
  asio::awaitable<bool> ping() {
    ++ping_calls;
    co_return is_open();
  }
  asio::awaitable<bool> disconnect() {
    connected = false;
    co_return true;
  }
};
}  // namespace pool_recovery_test

namespace ormpp {
template <>
struct db_execution_traits<pool_recovery_test::database> {
  static constexpr bool is_async = true;
};
}  // namespace ormpp

TEST_CASE("async_connection_pool: recover capacity after failed reconnect") {
  using database = pool_recovery_test::database;
  asio::io_context context;
  auto test = [&]() -> asio::awaitable<void> {
    auto executor = co_await asio::this_coro::executor;
    ormpp::pool_options options;
    options.log_pool_exhaustion = false;
    auto pool = std::make_shared<ormpp::async_connection_pool<database>>(
        executor, options);
    database::online = true;
    const auto initialized = co_await pool->init(2, "test");
    REQUIRE(initialized);

    // Failure is controlled by backend state, never a scheduling delay.
    database::online = false;
    auto unavailable = co_await pool->get(std::chrono::seconds(1));
    CHECK(unavailable == nullptr);
    auto [size, available, used, dynamic] = co_await pool->get_stats();
    CHECK(size == 2);
    CHECK(available == 2);
    CHECK(used == 0);

    database::online = true;
    auto connection = co_await pool->get(std::chrono::seconds(1));
    CHECK(connection != nullptr);
    connection.reset();
    co_await pool->close_all(true);
  };
  auto done = asio::co_spawn(context, test(), asio::use_future);
  pool_recovery_test::run(context, done);
}

TEST_CASE("async_connection_pool: close while reconnect is suspended") {
  using database = pool_recovery_test::database;
  asio::io_context context;
  auto test = [&]() -> asio::awaitable<void> {
    auto executor = co_await asio::this_coro::executor;
    ormpp::pool_options options;
    options.log_pool_exhaustion = false;
    auto pool = std::make_shared<ormpp::async_connection_pool<database>>(
        executor, options);
    database::online = true;
    const auto initialized = co_await pool->init(1, "test");
    REQUIRE(initialized);
    pool_recovery_test::event connecting(executor, 1), resume(executor, 1),
        finished(executor, 1);
    database::connecting = &connecting;
    database::resume_connect = &resume;
    database::online = false;
    std::exception_ptr error;
    std::shared_ptr<database> acquired;
    asio::co_spawn(
        executor, pool->get(std::chrono::seconds(1)),
        [&](std::exception_ptr e, std::shared_ptr<database> connection) {
          error = e;
          acquired = std::move(connection);
          finished.try_send(asio::error_code{});
        });
    co_await connecting.async_receive(asio::use_awaitable);
    co_await pool->close_all();
    resume.try_send(asio::error_code{});
    co_await finished.async_receive(asio::use_awaitable);
    database::connecting = nullptr;
    database::resume_connect = nullptr;
    database::online = true;
    CHECK(error == nullptr);
    CHECK(acquired == nullptr);
    auto [size, available, used, dynamic] = co_await pool->get_stats();
    CHECK(available == 0);
    CHECK(used == 0);
  };
  auto done = asio::co_spawn(context, test(), asio::use_future);
  pool_recovery_test::run(context, done);
}

TEST_CASE("async_connection_pool: returned lease serves an existing waiter") {
  using database = pool_recovery_test::database;
  asio::io_context context;
  auto test = [&]() -> asio::awaitable<void> {
    auto executor = co_await asio::this_coro::executor;
    auto pool =
        std::make_shared<ormpp::async_connection_pool<database>>(executor);
    database::online = true;
    const auto initialized = co_await pool->init(1, "test");
    REQUIRE(initialized);
    auto held = co_await pool->get();
    pool_recovery_test::event exhausted(executor, 2), finished(executor, 2);
    std::vector<std::string> order;
    auto callbacks = asio::make_strand(executor);
    auto complete = [&](std::string label) {
      return asio::bind_executor(
          callbacks, [&, label](std::exception_ptr error,
                                std::shared_ptr<database> connection) {
            order.push_back(error || !connection ? "failed" : label);
            finished.try_send(asio::error_code{});
          });
    };
    {
      pool_recovery_test::exhaustion_log observer(exhausted);
      asio::co_spawn(executor, pool->get(std::chrono::seconds(1)),
                     complete("older"));
      // Observe actual exhaustion, then cross an executor barrier. No sleep
      // or timing assumption establishes that the older request is waiting.
      co_await exhausted.async_receive(asio::use_awaitable);
      co_await pool->get_stats();
      held.reset();
      asio::co_spawn(executor, pool->get(std::chrono::seconds(1)),
                     complete("newer"));
      co_await finished.async_receive(asio::use_awaitable);
      co_await finished.async_receive(asio::use_awaitable);
    }
    REQUIRE(order.size() == 2);
    CHECK(order[0] == "older");
    CHECK(order[1] == "newer");
    co_await pool->close_all(true);
  };
  auto done = asio::co_spawn(context, test(), asio::use_future);
  pool_recovery_test::run(context, done);
}

TEST_CASE("async_connection_pool: queued cancellation and close") {
  using database = pool_recovery_test::database;
  bool closing = false;
  SUBCASE("cancel queued checkout") {}
  SUBCASE("close queued checkout") { closing = true; }
  asio::io_context context;
  auto test = [&]() -> asio::awaitable<void> {
    auto executor = co_await asio::this_coro::executor;
    auto pool =
        std::make_shared<ormpp::async_connection_pool<database>>(executor);
    database::online = true;
    const auto initialized = co_await pool->init(1, "test");
    REQUIRE(initialized);
    auto held = co_await pool->get();
    pool_recovery_test::event exhausted(executor, 1), finished(executor, 1);
    asio::cancellation_signal cancel;
    std::exception_ptr error;
    std::shared_ptr<database> acquired;
    {
      pool_recovery_test::exhaustion_log observer(exhausted);
      asio::co_spawn(
          executor, pool->get(std::chrono::seconds(1)),
          asio::bind_cancellation_slot(
              cancel.slot(),
              [&](std::exception_ptr e, std::shared_ptr<database> connection) {
                error = e;
                acquired = std::move(connection);
                finished.try_send(asio::error_code{});
              }));
      co_await exhausted.async_receive(asio::use_awaitable);
      co_await pool->get_stats();
      if (closing) {
        co_await pool->close_all();
      }
      else {
        cancel.emit(asio::cancellation_type::all);
      }
      co_await finished.async_receive(asio::use_awaitable);
    }
    CHECK((error == nullptr) == closing);
    CHECK(acquired == nullptr);
    held.reset();
    co_await pool->get_stats();
    if (closing) {
      const auto reinitialized = co_await pool->init(1, "test");
      CHECK(reinitialized);
    }
    auto next = co_await pool->get(std::chrono::seconds(1));
    CHECK(next != nullptr);
    next.reset();
    co_await pool->close_all(true);
  };
  auto done = asio::co_spawn(context, test(), asio::use_future);
  pool_recovery_test::run(context, done);
}

TEST_CASE("async_connection_pool: optional local connection probe") {
  using database = pool_recovery_test::database;
  bool ping = true;
  SUBCASE("default SQL ping") {}
  SUBCASE("local probe") { ping = false; }
  asio::io_context context;
  auto test = [&]() -> asio::awaitable<void> {
    auto executor = co_await asio::this_coro::executor;
    ormpp::pool_options options;
    options.log_pool_exhaustion = false;
    options.ping_on_checkout = ping;
    database::online = true;
    database::ping_calls = 0;
    auto pool = std::make_shared<ormpp::async_connection_pool<database>>(
        executor, options);
    const auto initialized = co_await pool->init(1, "test");
    REQUIRE(initialized);
    auto connection = co_await pool->get();
    REQUIRE(connection != nullptr);
    CHECK(database::ping_calls == (ping ? 1 : 0));
    co_await connection->disconnect();
    connection.reset();
    co_await pool->get_stats();
    auto reconnected = co_await pool->get();
    REQUIRE(reconnected != nullptr);
    CHECK(reconnected->is_open());
    CHECK(database::ping_calls == (ping ? 2 : 0));
    reconnected.reset();
    co_await pool->close_all(true);
  };
  auto done = asio::co_spawn(context, test(), asio::use_future);
  pool_recovery_test::run(context, done);
}

#endif
