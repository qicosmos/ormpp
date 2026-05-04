#ifdef ORMPP_ENABLE_MYSQL_ASYNC

#include <algorithm>
#include <asio.hpp>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

#include "async_connection_pool.hpp"
#include "dbng.hpp"
#include "doctest.h"
#include "mysql_async.hpp"
#include "ormpp_cfg.hpp"

using namespace ormpp;

namespace {

// 测试实体
struct test_person {
  int id;
  std::string name;
  int age;
};
REGISTER_AUTO_KEY(test_person, id)
YLT_REFL(test_person, id, name, age)

// 辅助函数：获取数据库连接参数
inline bool load_config_file(ormpp_cfg& cfg) {
  for (auto path :
       {"../cfg/ormpp.cfg", "cfg/ormpp.cfg", "../../cfg/ormpp.cfg"}) {
    if (config_manager::from_file(cfg, path)) {
      return true;
    }
  }
  return false;
}

inline std::tuple<std::string, std::string, std::string, std::string, int, int>
get_db_config() {
  ormpp_cfg cfg{};
  if (!load_config_file(cfg)) {
    cfg.db_ip = "127.0.0.1";
    cfg.user_name = "root";
    cfg.pwd = "";
    cfg.db_name = "test_ormppdb";
    cfg.timeout = 5;
    cfg.db_port = 3306;
  }

  const char* host = std::getenv("ORMPP_ASYNC_MYSQL_HOST");
  const char* user = std::getenv("ORMPP_ASYNC_MYSQL_USER");
  const char* password = std::getenv("ORMPP_ASYNC_MYSQL_PASSWORD");
  const char* db = std::getenv("ORMPP_ASYNC_MYSQL_DB");
  const char* port_str = std::getenv("ORMPP_ASYNC_MYSQL_PORT");

  return {host ? host : cfg.db_ip,
          user ? user : cfg.user_name,
          password ? password : cfg.pwd,
          db ? db : cfg.db_name,
          cfg.timeout,
          port_str ? std::atoi(port_str) : cfg.db_port};
}

inline bool getenv_enabled(const char* key) {
  const char* value = std::getenv(key);
  if (!value) {
    return false;
  }
  std::string item(value);
  return item == "1" || item == "true" || item == "TRUE" || item == "on" ||
         item == "ON";
}

inline std::size_t getenv_size(const char* key, std::size_t fallback) {
  const char* value = std::getenv(key);
  if (!value || *value == '\0') {
    return fallback;
  }
  return static_cast<std::size_t>(std::strtoull(value, nullptr, 10));
}

inline bool getenv_bool(const char* key, bool fallback) {
  const char* value = std::getenv(key);
  if (!value || *value == '\0') {
    return fallback;
  }
  std::string item(value);
  if (item == "1" || item == "true" || item == "TRUE" || item == "on" ||
      item == "ON") {
    return true;
  }
  if (item == "0" || item == "false" || item == "FALSE" || item == "off" ||
      item == "OFF") {
    return false;
  }
  return fallback;
}

// 辅助函数：创建并初始化连接池
asio::awaitable<std::shared_ptr<async_connection_pool<mysql_async>>>
create_test_pool(asio::any_io_executor executor, size_t pool_size = 5,
                 pool_options options = {}) {
  auto [host, user, password, db, timeout, port] = get_db_config();

  auto pool =
      std::make_shared<async_connection_pool<mysql_async>>(executor, options);

  bool success =
      co_await pool->init(pool_size, host, user, password, db, timeout, port);
  if (!success) {
    co_return nullptr;
  }

  co_return pool;
}

struct stress_options {
  std::size_t requests = 2000;
  std::size_t concurrency = 64;
  std::size_t pool_size = 50;
  bool dynamic_expansion = false;
  std::size_t dynamic_connections = 0;
  std::chrono::seconds get_timeout{10};
};

stress_options get_stress_options() {
  ormpp_cfg cfg{};
  if (!load_config_file(cfg) || cfg.db_conn_num <= 0) {
    cfg.db_conn_num = 50;
  }

  stress_options options;
  options.requests = getenv_size("ORMPP_STRESS_REQUESTS", options.requests);
  options.concurrency =
      getenv_size("ORMPP_STRESS_CONCURRENCY", options.concurrency);
  options.pool_size = getenv_size("ORMPP_STRESS_POOL_SIZE",
                                  static_cast<std::size_t>(cfg.db_conn_num));
  options.dynamic_expansion =
      getenv_bool("ORMPP_STRESS_DYNAMIC", options.dynamic_expansion);
  options.dynamic_connections =
      getenv_size("ORMPP_STRESS_DYNAMIC_MAX", options.dynamic_connections);
  options.get_timeout = std::chrono::seconds(
      getenv_size("ORMPP_STRESS_GET_TIMEOUT_SECONDS",
                  static_cast<std::size_t>(options.get_timeout.count())));

  options.requests = std::max<std::size_t>(1, options.requests);
  options.concurrency = std::max<std::size_t>(1, options.concurrency);
  options.pool_size = std::max<std::size_t>(1, options.pool_size);
  if (!options.dynamic_expansion) {
    options.dynamic_connections = 0;
  }
  return options;
}

asio::awaitable<void> pooled_select_worker(
    std::shared_ptr<async_connection_pool<mysql_async>> pool,
    std::atomic_size_t* next_request, std::size_t request_count,
    std::vector<std::int64_t>* latencies_us, std::atomic_size_t* successes,
    std::atomic_size_t* failures, std::atomic_size_t* workers_done,
    std::chrono::seconds get_timeout) {
  for (;;) {
    auto index = next_request->fetch_add(1, std::memory_order_relaxed);
    if (index >= request_count) {
      break;
    }

    auto begin = std::chrono::steady_clock::now();
    bool ok = false;
    try {
      auto conn = co_await pool->get(get_timeout);
      if (conn) {
        ok = co_await conn->execute("select 1");
        conn.reset();
      }
    } catch (...) {
      ok = false;
    }
    auto end = std::chrono::steady_clock::now();

    (*latencies_us)[index] =
        std::chrono::duration_cast<std::chrono::microseconds>(end - begin)
            .count();
    if (ok) {
      successes->fetch_add(1, std::memory_order_relaxed);
    }
    else {
      failures->fetch_add(1, std::memory_order_relaxed);
    }
  }

  workers_done->fetch_add(1, std::memory_order_release);
}

asio::awaitable<void> run_pooled_select_stress() {
  auto executor = co_await asio::this_coro::executor;
  auto stress = get_stress_options();

  pool_options options;
  options.enable_dynamic_expansion = stress.dynamic_expansion;
  options.max_dynamic_connections = stress.dynamic_connections;
  options.log_pool_exhaustion = false;

  auto pool = co_await create_test_pool(executor, stress.pool_size, options);
  REQUIRE(pool != nullptr);

  std::vector<std::int64_t> latencies_us(stress.requests, 0);
  std::atomic_size_t next_request{0};
  std::atomic_size_t successes{0};
  std::atomic_size_t failures{0};
  std::atomic_size_t workers_done{0};

  auto started = std::chrono::steady_clock::now();
  for (std::size_t i = 0; i < stress.concurrency; ++i) {
    asio::co_spawn(executor,
                   pooled_select_worker(pool, &next_request, stress.requests,
                                        &latencies_us, &successes, &failures,
                                        &workers_done, stress.get_timeout),
                   asio::detached);
  }

  while (workers_done.load(std::memory_order_acquire) < stress.concurrency) {
    asio::steady_timer timer(executor);
    timer.expires_after(std::chrono::milliseconds(20));
    co_await timer.async_wait(asio::use_awaitable);
  }
  auto finished = std::chrono::steady_clock::now();

  auto elapsed = std::chrono::duration_cast<std::chrono::duration<double>>(
                     finished - started)
                     .count();

  std::sort(latencies_us.begin(), latencies_us.end());
  auto percentile = [&](double p) {
    auto index = static_cast<std::size_t>(
        (p / 100.0) * static_cast<double>(latencies_us.size() - 1));
    return latencies_us[index];
  };
  auto total_latency = std::accumulate(latencies_us.begin(), latencies_us.end(),
                                       std::int64_t{0});
  auto avg_latency = static_cast<double>(total_latency) /
                     static_cast<double>(latencies_us.size());
  auto success_count = successes.load(std::memory_order_relaxed);
  auto failure_count = failures.load(std::memory_order_relaxed);
  auto qps = elapsed > 0.0 ? static_cast<double>(success_count) / elapsed : 0.0;

  auto [total, available, in_use, dynamic] = co_await pool->get_stats();

  std::cout << std::fixed << std::setprecision(2)
            << "\n[async mysql stress] pooled select 1\n"
            << "  requests=" << stress.requests
            << " concurrency=" << stress.concurrency
            << " pool_size=" << stress.pool_size
            << " dynamic=" << (stress.dynamic_expansion ? "on" : "off")
            << " dynamic_max=" << stress.dynamic_connections << "\n"
            << "  elapsed_s=" << elapsed << " success=" << success_count
            << " failure=" << failure_count << " qps=" << qps << "\n"
            << "  latency_us avg=" << avg_latency << " p50=" << percentile(50.0)
            << " p90=" << percentile(90.0) << " p95=" << percentile(95.0)
            << " p99=" << percentile(99.0) << " max=" << latencies_us.back()
            << "\n"
            << "  final_pool total=" << total << " available=" << available
            << " in_use=" << in_use << " dynamic=" << dynamic << std::endl;

  CHECK(failure_count == 0);
  CHECK(success_count == stress.requests);
  CHECK(available == stress.pool_size);
  CHECK(in_use == 0);

  co_await pool->close_all();
}

}  // namespace

TEST_CASE("async mysql stress: pooled select throughput") {
  if (!getenv_enabled("ORMPP_ASYNC_MYSQL_STRESS")) {
    MESSAGE("set ORMPP_ASYNC_MYSQL_STRESS=1 to run async MySQL stress test");
    return;
  }

  asio::io_context ctx;
  asio::co_spawn(ctx, run_pooled_select_stress(), asio::detached);
  ctx.run();
}

TEST_CASE("async_connection_pool: basic initialization") {
  asio::io_context ctx;

  auto test = [&]() -> asio::awaitable<void> {
    auto executor = co_await asio::this_coro::executor;

    SUBCASE("successful initialization") {
      auto pool = co_await create_test_pool(executor, 3);
      REQUIRE(pool != nullptr);

      auto [total, available, in_use, dynamic] = co_await pool->get_stats();
      CHECK(total == 3);
      CHECK(available == 3);
      CHECK(in_use == 0);
      CHECK(dynamic == 0);

      co_await pool->close_all();
    }

    SUBCASE("initialization with invalid credentials") {
      pool_options options;
      options.log_pool_exhaustion = false;  // 禁用日志避免干扰测试输出

      auto pool = std::make_shared<async_connection_pool<mysql_async>>(executor,
                                                                       options);

      bool success = co_await pool->init(2, "invalid_host", "invalid_user",
                                         "invalid_pass", "invalid_db", 1, 3306);
      CHECK_FALSE(success);
    }

    SUBCASE("double initialization") {
      auto pool = co_await create_test_pool(executor, 2);
      REQUIRE(pool != nullptr);

      // 第二次初始化应该返回 true（已经初始化）
      auto [host, user, password, db, timeout, port] = get_db_config();
      bool success =
          co_await pool->init(2, host, user, password, db, timeout, port);
      CHECK(success);

      co_await pool->close_all();
    }
  };

  asio::co_spawn(ctx, test(), asio::detached);
  ctx.run();
}

TEST_CASE("async_connection_pool: get and return connection") {
  asio::io_context ctx;

  auto test = [&]() -> asio::awaitable<void> {
    auto executor = co_await asio::this_coro::executor;
    auto pool = co_await create_test_pool(executor, 3);
    REQUIRE(pool != nullptr);

    SUBCASE("get single connection") {
      auto conn = co_await pool->get();
      REQUIRE(conn != nullptr);

      auto [total, available, in_use, dynamic] = co_await pool->get_stats();
      CHECK(total == 3);
      CHECK(available == 2);
      CHECK(in_use == 1);
      CHECK(dynamic == 0);

      // 连接自动归还
    }

    SUBCASE("get multiple connections") {
      auto conn1 = co_await pool->get();
      auto conn2 = co_await pool->get();
      auto conn3 = co_await pool->get();

      REQUIRE(conn1 != nullptr);
      REQUIRE(conn2 != nullptr);
      REQUIRE(conn3 != nullptr);

      auto [total, available, in_use, dynamic] = co_await pool->get_stats();
      CHECK(total == 3);
      CHECK(available == 0);
      CHECK(in_use == 3);

      // 连接自动归还
    }

    SUBCASE("connection auto-return on scope exit") {
      {
        auto conn = co_await pool->get();
        REQUIRE(conn != nullptr);

        auto [total, available, in_use, dynamic] = co_await pool->get_stats();
        CHECK(in_use == 1);
      }  // conn 析构，自动归还

      // 等待归还完成
      asio::steady_timer timer(executor);
      timer.expires_after(std::chrono::milliseconds(100));
      co_await timer.async_wait(asio::use_awaitable);

      auto [total, available, in_use, dynamic] = co_await pool->get_stats();
      CHECK(available == 3);
      CHECK(in_use == 0);
    }

    SUBCASE("use connection for database operations") {
      auto conn = co_await pool->get();
      REQUIRE(conn != nullptr);

      // 创建测试表
      co_await conn->execute("DROP TABLE IF EXISTS test_person");
      bool created =
          co_await conn->create_datatable<test_person>(ormpp_auto_key{"id"});
      CHECK(created);

      // 插入数据
      test_person p{0, "Alice", 25};
      int affected = co_await conn->insert(p);
      CHECK(affected == 1);

      // 查询数据
      auto persons = co_await conn->query_s<test_person>();
      CHECK(persons.size() == 1);
      CHECK(persons[0].name == "Alice");
    }

    co_await pool->close_all();
  };

  asio::co_spawn(ctx, test(), asio::detached);
  ctx.run();
}

TEST_CASE("async_connection_pool: pool exhaustion and timeout") {
  asio::io_context ctx;

  auto test = [&]() -> asio::awaitable<void> {
    auto executor = co_await asio::this_coro::executor;

    pool_options options;
    options.enable_dynamic_expansion = false;
    options.log_pool_exhaustion = false;

    auto pool = co_await create_test_pool(executor, 2, options);
    REQUIRE(pool != nullptr);

    SUBCASE("pool exhaustion with timeout") {
      // 获取所有连接
      auto conn1 = co_await pool->get();
      auto conn2 = co_await pool->get();

      REQUIRE(conn1 != nullptr);
      REQUIRE(conn2 != nullptr);

      // 尝试获取第三个连接（应该超时）
      auto conn3 = co_await pool->get(std::chrono::seconds(1));
      CHECK(conn3 == nullptr);

      auto [total, available, in_use, dynamic] = co_await pool->get_stats();
      CHECK(available == 0);
      CHECK(in_use == 2);
    }

    SUBCASE("connection becomes available after return") {
      auto conn1 = co_await pool->get();
      auto conn2 = co_await pool->get();

      REQUIRE(conn1 != nullptr);
      REQUIRE(conn2 != nullptr);

      // 启动一个协程，1 秒后释放连接
      asio::co_spawn(
          executor,
          [](std::shared_ptr<mysql_async> conn,
             asio::any_io_executor exec) -> asio::awaitable<void> {
            asio::steady_timer timer(exec);
            timer.expires_after(std::chrono::seconds(1));
            co_await timer.async_wait(asio::use_awaitable);
            conn.reset();
          }(std::move(conn1), executor),
          asio::detached);

      // 等待连接归还（2 秒超时）
      auto conn3 = co_await pool->get(std::chrono::seconds(2));
      CHECK(conn3 != nullptr);
    }

    co_await pool->close_all();
  };

  asio::co_spawn(ctx, test(), asio::detached);
  ctx.run();
}

TEST_CASE("async_connection_pool: dynamic expansion") {
  asio::io_context ctx;

  auto test = [&]() -> asio::awaitable<void> {
    auto executor = co_await asio::this_coro::executor;

    pool_options options;
    options.enable_dynamic_expansion = true;
    options.max_dynamic_connections = 2;
    options.log_pool_exhaustion = false;

    auto pool = co_await create_test_pool(executor, 2, options);
    REQUIRE(pool != nullptr);

    SUBCASE("create dynamic connection when pool exhausted") {
      // 获取所有固定连接
      auto conn1 = co_await pool->get();
      auto conn2 = co_await pool->get();

      REQUIRE(conn1 != nullptr);
      REQUIRE(conn2 != nullptr);

      auto [total1, available1, in_use1, dynamic1] = co_await pool->get_stats();
      CHECK(available1 == 0);
      CHECK(in_use1 == 2);
      CHECK(dynamic1 == 0);

      // 获取动态连接
      auto conn3 = co_await pool->get(std::chrono::seconds(2));
      REQUIRE(conn3 != nullptr);

      auto [total2, available2, in_use2, dynamic2] = co_await pool->get_stats();
      CHECK(available2 == 0);
      CHECK(in_use2 == 3);
      CHECK(dynamic2 == 1);
    }

    SUBCASE("dynamic connection limit") {
      // 获取所有固定连接
      auto conn1 = co_await pool->get();
      auto conn2 = co_await pool->get();

      // 获取最大数量的动态连接
      auto conn3 = co_await pool->get(std::chrono::seconds(2));
      auto conn4 = co_await pool->get(std::chrono::seconds(2));

      REQUIRE(conn3 != nullptr);
      REQUIRE(conn4 != nullptr);

      auto [total, available, in_use, dynamic] = co_await pool->get_stats();
      CHECK(dynamic == 2);

      // 尝试获取超过限制的动态连接（应该超时）
      auto conn5 = co_await pool->get(std::chrono::seconds(1));
      CHECK(conn5 == nullptr);
    }

    SUBCASE("dynamic connection auto-destroy") {
      {
        auto conn1 = co_await pool->get();
        auto conn2 = co_await pool->get();
        auto conn3 = co_await pool->get(std::chrono::seconds(2));  // 动态连接

        auto [total, available, in_use, dynamic] = co_await pool->get_stats();
        CHECK(dynamic == 1);
      }  // 所有连接析构

      // 等待归还完成
      asio::steady_timer timer(executor);
      timer.expires_after(std::chrono::milliseconds(100));
      co_await timer.async_wait(asio::use_awaitable);

      auto [total, available, in_use, dynamic] = co_await pool->get_stats();
      CHECK(available == 2);  // 固定连接归还
      CHECK(in_use == 0);
      CHECK(dynamic == 0);  // 动态连接销毁
    }

    co_await pool->close_all();
  };

  asio::co_spawn(ctx, test(), asio::detached);
  ctx.run();
}

TEST_CASE("async_connection_pool: connection health check") {
  asio::io_context ctx;

  auto test = [&]() -> asio::awaitable<void> {
    auto executor = co_await asio::this_coro::executor;
    auto pool = co_await create_test_pool(executor, 2);
    REQUIRE(pool != nullptr);

    SUBCASE("ping check on get") {
      auto conn = co_await pool->get();
      REQUIRE(conn != nullptr);

      // 连接应该是健康的
      bool alive = co_await conn->ping();
      CHECK(alive);
    }

    // 注意：测试连接失效后的重连比较困难，需要模拟网络断开
    // 这里只测试基本的 ping 功能

    co_await pool->close_all();
  };

  asio::co_spawn(ctx, test(), asio::detached);
  ctx.run();
}

TEST_CASE("async_connection_pool: concurrent access") {
  asio::io_context ctx;

  auto test = [&]() -> asio::awaitable<void> {
    auto executor = co_await asio::this_coro::executor;

    pool_options options;
    options.log_pool_exhaustion = false;

    auto pool = co_await create_test_pool(executor, 5, options);
    REQUIRE(pool != nullptr);

    SUBCASE("multiple concurrent gets") {
      std::atomic_size_t successful_gets{0};

      for (int i = 0; i < 10; ++i) {
        asio::co_spawn(
            executor,
            [](std::shared_ptr<async_connection_pool<mysql_async>> p,
               std::atomic_size_t& successes) -> asio::awaitable<void> {
              auto conn = co_await p->get(std::chrono::seconds(5));
              if (conn) {
                ++successes;
                // 模拟一些工作
                asio::steady_timer timer(co_await asio::this_coro::executor);
                timer.expires_after(std::chrono::milliseconds(100));
                co_await timer.async_wait(asio::use_awaitable);
              }
            }(pool, successful_gets),
            asio::detached);
      }

      // 所有连接应该已归还
      asio::steady_timer timer(executor);
      timer.expires_after(std::chrono::seconds(2));
      co_await timer.async_wait(asio::use_awaitable);

      auto [total, available, in_use, dynamic] = co_await pool->get_stats();
      CHECK(successful_gets == 10);
      CHECK(available == 5);
      CHECK(in_use == 0);
    }

    co_await pool->close_all();
  };

  asio::co_spawn(ctx, test(), asio::detached);
  ctx.run();
}

TEST_CASE("async_connection_pool: close_all") {
  asio::io_context ctx;

  auto test = [&]() -> asio::awaitable<void> {
    auto executor = co_await asio::this_coro::executor;
    auto pool = co_await create_test_pool(executor, 3);
    REQUIRE(pool != nullptr);

    SUBCASE("close_all without waiting") {
      auto conn = co_await pool->get();
      REQUIRE(conn != nullptr);

      // 不等待连接归还，直接关闭
      co_await pool->close_all(false);

      // 尝试获取连接应该失败（池已关闭）
      auto conn2 = co_await pool->get(std::chrono::seconds(1));
      CHECK(conn2 == nullptr);
    }

    SUBCASE("close_all with waiting") {
      auto conn = co_await pool->get();
      REQUIRE(conn != nullptr);

      // 启动一个协程，1 秒后释放连接
      asio::co_spawn(
          executor,
          [](std::shared_ptr<mysql_async> c,
             asio::any_io_executor exec) -> asio::awaitable<void> {
            asio::steady_timer timer(exec);
            timer.expires_after(std::chrono::seconds(1));
            co_await timer.async_wait(asio::use_awaitable);
            c.reset();
          }(std::move(conn), executor),
          asio::detached);

      // 等待连接归还后关闭
      co_await pool->close_all(true, std::chrono::seconds(5));

      // 池应该已关闭
      auto conn2 = co_await pool->get(std::chrono::seconds(1));
      CHECK(conn2 == nullptr);
    }

    SUBCASE("close_all timeout") {
      auto conn = co_await pool->get();
      REQUIRE(conn != nullptr);

      // 不归还连接，等待超时
      co_await pool->close_all(true, std::chrono::seconds(1));

      // 池应该已关闭（即使有连接未归还）
      // 注意：conn 仍然持有连接，但池已标记为未初始化
    }
  };

  asio::co_spawn(ctx, test(), asio::detached);
  ctx.run();
}

TEST_CASE("async_connection_pool: statistics") {
  asio::io_context ctx;

  auto test = [&]() -> asio::awaitable<void> {
    auto executor = co_await asio::this_coro::executor;

    pool_options options;
    options.enable_dynamic_expansion = true;
    options.max_dynamic_connections = 2;
    options.log_pool_exhaustion = false;

    auto pool = co_await create_test_pool(executor, 3, options);
    REQUIRE(pool != nullptr);

    SUBCASE("get_stats accuracy") {
      auto [total1, available1, in_use1, dynamic1] = co_await pool->get_stats();
      CHECK(total1 == 3);
      CHECK(available1 == 3);
      CHECK(in_use1 == 0);
      CHECK(dynamic1 == 0);

      auto conn1 = co_await pool->get();
      auto [total2, available2, in_use2, dynamic2] = co_await pool->get_stats();
      CHECK(available2 == 2);
      CHECK(in_use2 == 1);

      auto conn2 = co_await pool->get();
      auto conn3 = co_await pool->get();
      auto [total3, available3, in_use3, dynamic3] = co_await pool->get_stats();
      CHECK(available3 == 0);
      CHECK(in_use3 == 3);

      // 获取动态连接
      auto conn4 = co_await pool->get(std::chrono::seconds(2));
      auto [total4, available4, in_use4, dynamic4] = co_await pool->get_stats();
      CHECK(in_use4 == 4);
      CHECK(dynamic4 == 1);
    }

    SUBCASE("get_status_string") {
      auto status = co_await pool->get_status_string();
      CHECK(status.find("Pool size:") != std::string::npos);
      CHECK(status.find("Available:") != std::string::npos);
      CHECK(status.find("In use:") != std::string::npos);
      CHECK(status.find("Dynamic:") != std::string::npos);
      CHECK(status.find("Utilization:") != std::string::npos);
    }

    co_await pool->close_all();
  };

  asio::co_spawn(ctx, test(), asio::detached);
  ctx.run();
}

TEST_CASE("async_connection_pool: edge cases") {
  asio::io_context ctx;

  auto test = [&]() -> asio::awaitable<void> {
    auto executor = co_await asio::this_coro::executor;

    SUBCASE("zero pool size") {
      pool_options options;
      options.log_pool_exhaustion = false;

      auto [host, user, password, db, timeout, port] = get_db_config();
      auto pool = std::make_shared<async_connection_pool<mysql_async>>(executor,
                                                                       options);

      // 初始化大小为 0 的池
      bool success =
          co_await pool->init(0, host, user, password, db, timeout, port);
      CHECK(success);

      // 获取连接应该超时
      auto conn = co_await pool->get(std::chrono::seconds(1));
      CHECK(conn == nullptr);

      co_await pool->close_all();
    }

    SUBCASE("very short timeout") {
      auto pool = co_await create_test_pool(executor, 1);
      REQUIRE(pool != nullptr);

      auto conn1 = co_await pool->get();
      REQUIRE(conn1 != nullptr);

      // 极短超时
      auto conn2 = co_await pool->get(std::chrono::seconds(0));
      CHECK(conn2 == nullptr);

      co_await pool->close_all();
    }

    SUBCASE("get from uninitialized pool") {
      pool_options options;
      options.log_pool_exhaustion = false;

      auto pool = std::make_shared<async_connection_pool<mysql_async>>(executor,
                                                                       options);

      // 未初始化就获取连接
      auto conn = co_await pool->get(std::chrono::seconds(1));
      CHECK(conn == nullptr);
    }

    SUBCASE("multiple close_all calls") {
      auto pool = co_await create_test_pool(executor, 2);
      REQUIRE(pool != nullptr);

      co_await pool->close_all();
      co_await pool->close_all();  // 第二次调用应该安全

      auto conn = co_await pool->get(std::chrono::seconds(1));
      CHECK(conn == nullptr);
    }
  };

  asio::co_spawn(ctx, test(), asio::detached);
  ctx.run();
}

TEST_CASE("async_connection_pool: destructor cleanup") {
  asio::io_context ctx;

  auto test = [&]() -> asio::awaitable<void> {
    auto executor = co_await asio::this_coro::executor;

    SUBCASE("pool destroyed with connections in use") {
      std::shared_ptr<mysql_async> conn;
      {
        auto pool = co_await create_test_pool(executor, 2);
        REQUIRE(pool != nullptr);

        conn = co_await pool->get();
        REQUIRE(conn != nullptr);

        // pool 超出作用域，但 conn 仍然持有连接
      }  // pool 析构

      conn.reset();

      // 等待一下确保析构完成
      asio::steady_timer timer(executor);
      timer.expires_after(std::chrono::milliseconds(100));
      co_await timer.async_wait(asio::use_awaitable);

      // 测试通过表示没有崩溃
      CHECK(true);
    }

    SUBCASE("pool destroyed after close_all") {
      {
        auto pool = co_await create_test_pool(executor, 2);
        REQUIRE(pool != nullptr);

        co_await pool->close_all();
      }  // pool 析构

      CHECK(true);
    }
  };

  asio::co_spawn(ctx, test(), asio::detached);
  ctx.run();
}

TEST_CASE("async_connection_pool: exponential backoff") {
  asio::io_context ctx;

  auto test = [&]() -> asio::awaitable<void> {
    auto executor = co_await asio::this_coro::executor;

    pool_options options;
    options.enable_dynamic_expansion = false;
    options.log_pool_exhaustion = false;

    auto pool = co_await create_test_pool(executor, 1, options);
    REQUIRE(pool != nullptr);

    SUBCASE("backoff timing") {
      auto conn1 = co_await pool->get();
      REQUIRE(conn1 != nullptr);

      auto start = std::chrono::steady_clock::now();

      // 尝试获取第二个连接（应该等待并超时）
      auto conn2 = co_await pool->get(std::chrono::seconds(2));
      CHECK(conn2 == nullptr);

      auto elapsed = std::chrono::steady_clock::now() - start;
      auto elapsed_ms =
          std::chrono::duration_cast<std::chrono::milliseconds>(elapsed)
              .count();

      // 应该等待了接近 2 秒
      CHECK(elapsed_ms >= 1900);  // 允许一些误差
      CHECK(elapsed_ms <= 2200);
    }

    co_await pool->close_all();
  };

  asio::co_spawn(ctx, test(), asio::detached);
  ctx.run();
}

#endif  // ORMPP_ENABLE_MYSQL_ASYNC
