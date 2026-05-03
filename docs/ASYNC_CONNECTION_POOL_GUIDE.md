# ORMPP 异步连接池使用指南

## 概述

`async_connection_pool` 是为 ORMPP 异步数据库接口（如 `mysql_async`）设计的协程连接池。它提供了高效的连接复用、自动重连、线程安全的连接管理等特性。

## 特性

✅ **协程友好** - 完全基于 Asio 协程实现
✅ **自动重连** - 检测到连接失效时自动重连
✅ **RAII 管理** - 使用 `shared_ptr` 自定义删除器自动归还连接
✅ **线程安全** - 使用 `asio::strand` 保证线程安全
✅ **超时控制** - 支持获取连接时的超时设置
✅ **统计信息** - 可查询连接池状态

## 基础使用

### 1. 创建和初始化连接池

```cpp
#include <asio.hpp>
#include "mysql_async.hpp"
#include "async_connection_pool.hpp"

using namespace ormpp;

asio::awaitable<void> example() {
  auto executor = co_await asio::this_coro::executor;

  // 创建连接池（需要使用 shared_ptr）
  auto pool = std::make_shared<async_connection_pool<mysql_async>>(executor);

  // 初始化连接池（10 个连接）
  bool success = co_await pool->init(
    10,              // 连接池大小
    "127.0.0.1",     // host
    "root",          // user
    "password",      // password
    "test_db",       // database
    5,               // timeout (seconds)
    3306             // port
  );

  if (!success) {
    std::cerr << "Failed to initialize connection pool" << std::endl;
    co_return;
  }

  std::cout << "Connection pool initialized successfully" << std::endl;
}
```

### 2. 获取和使用连接

```cpp
asio::awaitable<void> use_connection(
    std::shared_ptr<async_connection_pool<mysql_async>> pool) {

  // 获取连接（默认 10 秒超时）
  auto conn = co_await pool->get();

  if (!conn) {
    std::cerr << "Failed to get connection (timeout)" << std::endl;
    co_return;
  }

  // 执行数据库操作
  auto persons = co_await conn->query_s<Person>();

  for (const auto& p : persons) {
    std::cout << p.name << ", " << p.age << std::endl;
  }

  // conn 析构时自动归还连接到池中
}
```

### 3. 自定义超时时间

```cpp
asio::awaitable<void> use_connection_with_timeout(
    std::shared_ptr<async_connection_pool<mysql_async>> pool) {

  // 获取连接（5 秒超时）
  auto conn = co_await pool->get(std::chrono::seconds(5));

  if (!conn) {
    std::cerr << "Connection timeout after 5 seconds" << std::endl;
    co_return;
  }

  // 使用连接...
}
```

## 连接池耗尽处理机制

### 处理流程

当连接池中所有连接都在使用中时，`get()` 方法会按以下流程处理：

```
获取连接 get()
    ↓
检查连接池
    ↓
有可用连接？
    ├─ 是 → 验证连接健康 → 返回连接
    └─ 否 → 连接池耗尽
            ↓
        启用动态扩容？
            ├─ 是 → 创建临时连接 → 返回临时连接
            └─ 否 → 继续
                    ↓
                记录日志（第一次）
                    ↓
                智能等待（指数退避）
                    ↓
                超时？
                    ├─ 否 → 重试获取连接
                    └─ 是 → 记录超时日志 → 返回 nullptr
```

### 三层处理策略

#### 1. 动态扩容（可选）

当启用动态扩容时，连接池耗尽时会创建临时连接：

```cpp
pool_options options;
options.enable_dynamic_expansion = true;   // 启用动态扩容
options.max_dynamic_connections = 5;       // 最多 5 个临时连接

auto pool = std::make_shared<async_connection_pool<mysql_async>>(
    executor, options
);
```

**特点**：
- 临时连接不占用固定池大小
- 使用完后自动销毁（不归还池中）
- 避免长时间等待，提高并发能力

**适用场景**：
- 突发流量高峰
- 偶尔的并发峰值
- 需要快速响应的场景

#### 2. 智能等待（指数退避）

当没有可用连接且无法创建临时连接时，使用指数退避策略等待：

```cpp
// 等待时间序列
第 1 次：50ms
第 2 次：100ms
第 3 次：200ms
第 4 次及以后：500ms (最大)
```

**优点**：
- 快速响应连接归还（初始等待时间短）
- 避免频繁轮询浪费 CPU（后期等待时间长）
- 不阻塞事件循环（使用 asio::steady_timer）

#### 3. 超时保护

默认 10 秒超时，可自定义：

```cpp
// 短超时（快速失败）
auto conn = co_await pool->get(std::chrono::seconds(5));

// 长超时（容忍等待）
auto conn = co_await pool->get(std::chrono::seconds(30));

// 极短超时（立即失败）
auto conn = co_await pool->get(std::chrono::seconds(1));
```

### 日志和监控

#### 启用日志

```cpp
pool_options options;
options.log_pool_exhaustion = true;  // 启用日志（默认开启）

auto pool = std::make_shared<async_connection_pool<mysql_async>>(
    executor, options
);
```

#### 日志输出示例

```
[Connection Pool] Pool exhausted - all 10 connections in use.
                  Waiting for available connection...

[Connection Pool] Timeout after 10 seconds waiting for connection.
                  Consider increasing pool size or timeout.
```

### 处理连接池耗尽

#### 示例 1：基本错误处理

```cpp
asio::awaitable<void> handle_exhaustion(
    std::shared_ptr<async_connection_pool<mysql_async>> pool) {

    auto conn = co_await pool->get(std::chrono::seconds(10));

    if (!conn) {
        // 连接池耗尽且超时
        std::cerr << "Connection pool exhausted!" << std::endl;

        // 查看统计信息
        auto [total, available, in_use, dynamic] = co_await pool->get_stats();
        std::cerr << "Pool stats:" << std::endl;
        std::cerr << "  Total: " << total << std::endl;
        std::cerr << "  Available: " << available << std::endl;
        std::cerr << "  In use: " << in_use << std::endl;
        std::cerr << "  Dynamic: " << dynamic << std::endl;

        co_return;
    }

    // 使用连接...
}
```

#### 示例 2：重试机制

```cpp
asio::awaitable<std::shared_ptr<mysql_async>> get_connection_with_retry(
    std::shared_ptr<async_connection_pool<mysql_async>> pool,
    int max_retries = 3) {

    for (int i = 0; i < max_retries; ++i) {
        auto conn = co_await pool->get(std::chrono::seconds(5));

        if (conn) {
            co_return conn;
        }

        std::cerr << "Retry " << (i + 1) << "/" << max_retries << std::endl;

        // 等待一段时间后重试
        asio::steady_timer timer(co_await asio::this_coro::executor);
        timer.expires_after(std::chrono::seconds(1));
        co_await timer.async_wait(asio::use_awaitable);
    }

    co_return nullptr;
}
```

#### 示例 3：降级策略

```cpp
asio::awaitable<void> query_with_fallback(
    std::shared_ptr<async_connection_pool<mysql_async>> pool) {

    auto conn = co_await pool->get(std::chrono::seconds(5));

    if (!conn) {
        // 连接池耗尽，使用降级策略
        std::cerr << "Using fallback: cached data" << std::endl;

        // 返回缓存数据
        auto cached_data = get_cached_data();
        process_data(cached_data);

        co_return;
    }

    // 正常查询数据库
    auto data = co_await conn->query_s<Data>();
    process_data(data);

    // 更新缓存
    update_cache(data);
}
```

### 配置建议

#### 1. 固定池大小

```cpp
// 根据并发需求设置
// 推荐：CPU 核心数 * 2 到 CPU 核心数 * 4
size_t pool_size = std::thread::hardware_concurrency() * 2;

co_await pool->init(pool_size, "localhost", "root", "password", "db");
```

#### 2. 动态扩容配置

```cpp
pool_options options;

// 低并发场景：不启用动态扩容
options.enable_dynamic_expansion = false;

// 中等并发场景：启用少量动态连接
options.enable_dynamic_expansion = true;
options.max_dynamic_connections = 5;

// 高并发场景：启用较多动态连接
options.enable_dynamic_expansion = true;
options.max_dynamic_connections = 20;
```

#### 3. 超时配置

```cpp
// 快速失败场景（如 API 请求）
auto conn = co_await pool->get(std::chrono::seconds(3));

// 批处理场景（可容忍等待）
auto conn = co_await pool->get(std::chrono::seconds(30));

// 实时场景（立即失败）
auto conn = co_await pool->get(std::chrono::seconds(1));
```

### 诊断和优化

#### 监控连接池状态

```cpp
asio::awaitable<void> monitor_pool(
    std::shared_ptr<async_connection_pool<mysql_async>> pool) {

    while (true) {
        auto [total, available, in_use, dynamic] = co_await pool->get_stats();

        // 计算利用率
        double utilization = (double)in_use / (total + dynamic) * 100;

        std::cout << "Pool utilization: " << utilization << "%" << std::endl;

        // 警告：利用率过高
        if (utilization > 90) {
            std::cerr << "Warning: Pool utilization > 90%!" << std::endl;
            std::cerr << "Consider increasing pool size." << std::endl;
        }

        // 每 5 秒检查一次
        asio::steady_timer timer(co_await asio::this_coro::executor);
        timer.expires_after(std::chrono::seconds(5));
        co_await timer.async_wait(asio::use_awaitable);
    }
}
```

#### 检测连接泄漏

```cpp
asio::awaitable<void> check_connection_leak(
    std::shared_ptr<async_connection_pool<mysql_async>> pool) {

    auto [total, available, in_use, dynamic] = co_await pool->get_stats();

    // 如果长时间大部分连接都在使用中，可能存在连接泄漏
    if (in_use > total * 0.9) {
        std::cerr << "Warning: Possible connection leak detected!" << std::endl;
        std::cerr << "  " << in_use << " / " << total
                  << " connections in use for extended period" << std::endl;

        // 检查代码中是否有：
        // 1. 连接未及时归还（作用域过大）
        // 2. 异常处理不当导致连接未释放
        // 3. 长时间运行的查询
    }
}
```

## 完整示例

### 示例 1：基本 CRUD 操作

```cpp
#include <asio.hpp>
#include <iostream>
#include "mysql_async.hpp"
#include "async_connection_pool.hpp"

using namespace ormpp;

struct Person {
  int id;
  std::string name;
  int age;
};
REGISTER_AUTO_KEY(Person, id)
YLT_REFL(Person, id, name, age)

// 插入数据
asio::awaitable<void> insert_person(
    std::shared_ptr<async_connection_pool<mysql_async>> pool,
    const Person& person) {

  auto conn = co_await pool->get();
  if (!conn) {
    std::cerr << "Failed to get connection" << std::endl;
    co_return;
  }

  int affected = co_await conn->insert(person);
  std::cout << "Inserted " << affected << " rows" << std::endl;
}

// 查询数据
asio::awaitable<std::vector<Person>> query_persons(
    std::shared_ptr<async_connection_pool<mysql_async>> pool) {

  auto conn = co_await pool->get();
  if (!conn) {
    co_return std::vector<Person>{};
  }

  co_return co_await conn->query_s<Person>();
}

// 更新数据
asio::awaitable<void> update_person(
    std::shared_ptr<async_connection_pool<mysql_async>> pool,
    int id, int new_age) {

  auto conn = co_await pool->get();
  if (!conn) {
    co_return;
  }

  int affected = co_await conn->update<Person>()
                       .set(col(&Person::age), new_age)
                       .where(col(&Person::id) == id)
                       .execute();

  std::cout << "Updated " << affected << " rows" << std::endl;
}

// 删除数据
asio::awaitable<void> delete_person(
    std::shared_ptr<async_connection_pool<mysql_async>> pool,
    int id) {

  auto conn = co_await pool->get();
  if (!conn) {
    co_return;
  }

  int affected = co_await conn->remove<Person>()
                       .where(col(&Person::id) == id)
                       .execute();

  std::cout << "Deleted " << affected << " rows" << std::endl;
}

// 主函数
asio::awaitable<void> main_coroutine() {
  auto executor = co_await asio::this_coro::executor;

  // 创建连接池
  auto pool = std::make_shared<async_connection_pool<mysql_async>>(executor);

  // 初始化连接池
  if (!co_await pool->init(5, "localhost", "root", "password", "test_db")) {
    std::cerr << "Failed to initialize pool" << std::endl;
    co_return;
  }

  // 创建表
  {
    auto conn = co_await pool->get();
    if (conn) {
      co_await conn->execute("DROP TABLE IF EXISTS Person");
      co_await conn->create_datatable<Person>(ormpp_auto_key{"id"});
    }
  }

  // 插入数据
  co_await insert_person(pool, Person{0, "Alice", 25});
  co_await insert_person(pool, Person{0, "Bob", 30});
  co_await insert_person(pool, Person{0, "Charlie", 28});

  // 查询数据
  auto persons = co_await query_persons(pool);
  std::cout << "\nAll persons:" << std::endl;
  for (const auto& p : persons) {
    std::cout << "  " << p.name << " (" << p.age << ")" << std::endl;
  }

  // 更新数据
  co_await update_person(pool, 1, 26);

  // 删除数据
  co_await delete_person(pool, 2);

  // 查询最终结果
  persons = co_await query_persons(pool);
  std::cout << "\nFinal persons:" << std::endl;
  for (const auto& p : persons) {
    std::cout << "  " << p.name << " (" << p.age << ")" << std::endl;
  }

  // 查看连接池统计
  auto [total, available, in_use, dynamic] = co_await pool->get_stats();
  std::cout << "\nPool stats:" << std::endl;
  std::cout << "  Total: " << total << std::endl;
  std::cout << "  Available: " << available << std::endl;
  std::cout << "  In use: " << in_use << std::endl;
  std::cout << "  Dynamic: " << dynamic << std::endl;

  // 关闭连接池
  co_await pool->close_all();
}

int main() {
  try {
    asio::io_context ctx;
    asio::co_spawn(ctx, main_coroutine(), asio::detached);
    ctx.run();
  } catch (const std::exception& e) {
    std::cerr << "Exception: " << e.what() << std::endl;
    return 1;
  }
  return 0;
}
```

### 示例 2：并发操作

```cpp
asio::awaitable<void> concurrent_operations(
    std::shared_ptr<async_connection_pool<mysql_async>> pool) {

  // 启动多个并发任务
  std::vector<asio::awaitable<void>> tasks;

  for (int i = 0; i < 10; ++i) {
    tasks.push_back(insert_person(pool, Person{0, "User" + std::to_string(i), 20 + i}));
  }

  // 等待所有任务完成
  for (auto& task : tasks) {
    co_await std::move(task);
  }

  std::cout << "All concurrent operations completed" << std::endl;
}
```

### 示例 3：事务处理

```cpp
asio::awaitable<void> transaction_example(
    std::shared_ptr<async_connection_pool<mysql_async>> pool) {

  auto conn = co_await pool->get();
  if (!conn) {
    co_return;
  }

  try {
    // 开始事务
    if (!co_await conn->begin()) {
      throw std::runtime_error("Failed to begin transaction");
    }

    // 执行多个操作
    co_await conn->insert(Person{0, "Alice", 25});
    co_await conn->insert(Person{0, "Bob", 30});

    // 提交事务
    if (!co_await conn->commit()) {
      throw std::runtime_error("Failed to commit transaction");
    }

    std::cout << "Transaction committed successfully" << std::endl;

  } catch (const std::exception& e) {
    // 回滚事务
    co_await conn->rollback();
    std::cerr << "Transaction rolled back: " << e.what() << std::endl;
  }
}
```

### 示例 4：连接池统计和监控

```cpp
asio::awaitable<void> monitor_pool(
    std::shared_ptr<async_connection_pool<mysql_async>> pool) {

  while (true) {
    auto [total, available, in_use, dynamic] = co_await pool->get_stats();

    std::cout << "Pool stats - Total: " << total
              << ", Available: " << available
              << ", In use: " << in_use
              << ", Dynamic: " << dynamic << std::endl;

    // 每 5 秒检查一次
    asio::steady_timer timer(co_await asio::this_coro::executor);
    timer.expires_after(std::chrono::seconds(5));
    co_await timer.async_wait(asio::use_awaitable);
  }
}
```

## 高级用法

### 1. 连接池作为全局单例

```cpp
class DatabaseService {
 public:
  static DatabaseService& instance() {
    static DatabaseService instance;
    return instance;
  }

  asio::awaitable<bool> init(asio::any_io_executor executor) {
    if (pool_) {
      co_return true;
    }

    pool_ = std::make_shared<async_connection_pool<mysql_async>>(executor);
    co_return co_await pool_->init(10, "localhost", "root", "password", "db");
  }

  std::shared_ptr<async_connection_pool<mysql_async>> get_pool() {
    return pool_;
  }

 private:
  DatabaseService() = default;
  std::shared_ptr<async_connection_pool<mysql_async>> pool_;
};

// 使用
asio::awaitable<void> use_service() {
  auto executor = co_await asio::this_coro::executor;

  // 初始化服务
  co_await DatabaseService::instance().init(executor);

  // 获取连接池
  auto pool = DatabaseService::instance().get_pool();

  // 使用连接池
  auto conn = co_await pool->get();
  // ...
}
```

### 2. 连接池配置类

```cpp
struct PoolConfig {
  size_t pool_size = 10;
  std::string host = "localhost";
  std::string user = "root";
  std::string password = "";
  std::string database = "test";
  std::optional<int> timeout = 5;
  std::optional<int> port = 3306;
  std::chrono::seconds connection_timeout = std::chrono::seconds(10);
};

asio::awaitable<std::shared_ptr<async_connection_pool<mysql_async>>>
create_pool(asio::any_io_executor executor, const PoolConfig& config) {
  auto pool = std::make_shared<async_connection_pool<mysql_async>>(executor);

  bool success = co_await pool->init(
    config.pool_size,
    config.host,
    config.user,
    config.password,
    config.database,
    config.timeout,
    config.port
  );

  if (!success) {
    co_return nullptr;
  }

  co_return pool;
}
```

### 3. 错误处理和重试

```cpp
asio::awaitable<std::shared_ptr<mysql_async>> get_connection_with_retry(
    std::shared_ptr<async_connection_pool<mysql_async>> pool,
    int max_retries = 3) {

    for (int i = 0; i < max_retries; ++i) {
        auto conn = co_await pool->get(std::chrono::seconds(5));

        if (conn) {
            co_return conn;
        }

        std::cerr << "Retry " << (i + 1) << "/" << max_retries << std::endl;

        // 等待一段时间后重试
        asio::steady_timer timer(co_await asio::this_coro::executor);
        timer.expires_after(std::chrono::seconds(1));
        co_await timer.async_wait(asio::use_awaitable);
    }

    co_return nullptr;
}
```

## 性能优化建议

### 1. 合理设置连接池大小

```cpp
// 根据并发需求设置连接池大小
// 一般建议：CPU 核心数 * 2 到 CPU 核心数 * 4
size_t pool_size = std::thread::hardware_concurrency() * 2;

auto pool = std::make_shared<async_connection_pool<mysql_async>>(executor);
co_await pool->init(pool_size, "localhost", "root", "password", "db");
```

### 2. 及时归还连接

```cpp
// ✅ 推荐：使用作用域限制连接生命周期
{
  auto conn = co_await pool->get();
  if (conn) {
    // 使用连接...
  }
  // conn 自动归还
}

// ❌ 不推荐：长时间持有连接
auto conn = co_await pool->get();
// ... 大量其他操作 ...
// 连接被长时间占用
```

### 3. 批量操作

```cpp
// ✅ 推荐：在一个连接中执行批量操作
asio::awaitable<void> batch_insert(
    std::shared_ptr<async_connection_pool<mysql_async>> pool,
    const std::vector<Person>& persons) {

  auto conn = co_await pool->get();
  if (!conn) {
    co_return;
  }

  // 批量插入
  co_await conn->insert(persons);
}

// ❌ 不推荐：每次插入都获取新连接
asio::awaitable<void> single_insert(
    std::shared_ptr<async_connection_pool<mysql_async>> pool,
    const std::vector<Person>& persons) {

  for (const auto& p : persons) {
    auto conn = co_await pool->get();
    if (conn) {
      co_await conn->insert(p);
    }
  }
}
```

## 常见问题

### 1. 连接池耗尽怎么办？

```cpp
auto conn = co_await pool->get(std::chrono::seconds(30));

if (!conn) {
  // 连接池耗尽，考虑：
  // 1. 增加连接池大小
  // 2. 检查是否有连接泄漏（未归还）
  // 3. 优化查询性能，减少连接占用时间
  std::cerr << "Connection pool exhausted" << std::endl;
}
```

### 2. 如何检测连接泄漏？

```cpp
asio::awaitable<void> check_pool_health(
    std::shared_ptr<async_connection_pool<mysql_async>> pool) {

  auto [total, available, in_use, dynamic] = co_await pool->get_stats();

  // 如果长时间 in_use 接近 total，可能存在连接泄漏
  if (in_use > total * 0.9) {
    std::cerr << "Warning: Most connections are in use!" << std::endl;
    std::cerr << "  Total: " << total << std::endl;
    std::cerr << "  Available: " << available << std::endl;
    std::cerr << "  In use: " << in_use << std::endl;
    std::cerr << "  Dynamic: " << dynamic << std::endl;
  }
}
```

### 3. 如何优雅关闭？

```cpp
asio::awaitable<void> graceful_shutdown(
    std::shared_ptr<async_connection_pool<mysql_async>> pool) {

  std::cout << "Shutting down connection pool..." << std::endl;

  // 等待所有连接归还（最多等待 30 秒）
  auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);

  while (std::chrono::steady_clock::now() < deadline) {
    auto [total, available, in_use, dynamic] = co_await pool->get_stats();

    if (in_use == 0) {
      break;
    }

    std::cout << "Waiting for " << in_use << " connections to be returned..."
              << std::endl;

    asio::steady_timer timer(co_await asio::this_coro::executor);
    timer.expires_after(std::chrono::seconds(1));
    co_await timer.async_wait(asio::use_awaitable);
  }

  // 关闭所有连接
  co_await pool->close_all();

  std::cout << "Connection pool closed" << std::endl;
}
```

## 编译和运行

```bash
# 编译
g++ -std=c++20 -I/path/to/ormpp -I/path/to/asio \
    example.cpp -lssl -lcrypto -pthread -o example

# 运行
./example
```

## 总结

ORMPP 异步连接池提供了：

1. ✅ **高性能** - 连接复用，减少连接开销
2. ✅ **易用性** - RAII 自动管理，无需手动归还
3. ✅ **可靠性** - 自动重连，连接健康检查
4. ✅ **并发安全** - 基于 strand 的线程安全设计
5. ✅ **协程友好** - 完全异步，不阻塞事件循环

通过合理使用连接池，可以显著提升应用的性能和稳定性！
