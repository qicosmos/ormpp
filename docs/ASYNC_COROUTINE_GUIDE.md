# ORMPP 异步协程使用指南

## 概述

ORMPP 通过 Asio 协程提供了完整的异步 MySQL 数据库访问接口。本文档介绍如何使用 C++20 协程与 ORMPP 异步接口进行数据库操作。

## 前置要求

### 1. 编译器支持
- GCC 10+ 或 Clang 12+ 或 MSVC 2019+
- 需要 C++20 协程支持

### 2. 依赖库
```bash
# Ubuntu/Debian
sudo apt-get install libasio-dev libssl-dev

# macOS
brew install asio openssl

# Arch Linux
sudo pacman -S asio openssl
```

### 3. CMake 配置
```bash
mkdir build && cd build
cmake .. -DENABLE_MYSQL_ASYNC=ON
make
```

## 核心概念

### 1. 异步数据库类型

ORMPP 提供了 `mysql_async` 类，它是基于 Asio 协程的异步 MySQL 客户端：

```cpp
#include "mysql_async.hpp"
#include "dbng.hpp"
#include <asio.hpp>

using namespace ormpp;

// 使用 dbng 包装类（推荐）
dbng<mysql_async> db(executor);

// 或直接使用底层类
mysql_async db(executor);
```

### 2. 协程返回类型

所有异步操作返回 `asio::awaitable<T>`：

```cpp
// 连接数据库
asio::awaitable<bool> connect_result = db.connect(...);

// 查询数据
asio::awaitable<std::vector<Person>> query_result = db.query_s<Person>();

// 插入数据
asio::awaitable<int> insert_result = db.insert(person);
```

### 3. 使用 co_await

在协程函数中使用 `co_await` 等待异步操作完成：

```cpp
asio::awaitable<void> my_database_operation(dbng<mysql_async>& db) {
  // 等待查询完成
  auto persons = co_await db.query_s<Person>();

  // 处理结果...
}
```

### 4. 简化 Executor 传递（推荐模式）

为了避免每个函数都写 `auto executor = co_await asio::this_coro::executor;`，推荐将 db 作为参数传递：

```cpp
// 方式 1：将 db 作为参数传递（推荐）
asio::awaitable<void> query_operations(dbng<mysql_async>& db) {
  // 直接使用 db，无需获取 executor
  auto result = co_await db.query_s<Person>();
}

// 方式 2：将 executor 作为参数传递
asio::awaitable<void> query_operations(asio::any_io_executor executor) {
  dbng<mysql_async> db(executor);
  co_await db.connect(...);
  // 执行操作...
}

// 主协程中只获取一次 executor
asio::awaitable<void> main_coroutine() {
  auto executor = co_await asio::this_coro::executor;
  dbng<mysql_async> db(executor);
  co_await db.connect(...);

  // 调用其他函数，传递 db
  co_await query_operations(db);
}
```

## 基础使用示例

### 1. 完整的异步程序结构

```cpp
#include <asio.hpp>
#include "dbng.hpp"
#include "mysql_async.hpp"

using namespace ormpp;

// 定义实体
struct Person {
  int id;
  std::string name;
  int age;
};
REGISTER_AUTO_KEY(Person, id)
YLT_REFL(Person, id, name, age)

// 数据库操作函数（接受 db 作为参数）
asio::awaitable<void> database_operations(dbng<mysql_async>& db) {
  // 创建表
  co_await db.create_datatable<Person>(ormpp_auto_key{"id"});

  // 插入数据
  Person p{0, "Alice", 25};
  int affected = co_await db.insert(p);
  std::cout << "Inserted " << affected << " rows" << std::endl;

  // 查询数据
  auto persons = co_await db.query_s<Person>();
  for (const auto& person : persons) {
    std::cout << person.name << ", " << person.age << std::endl;
  }
}

// 主协程（只在这里获取 executor 和创建连接）
asio::awaitable<void> main_coroutine() {
  // 获取 executor
  auto executor = co_await asio::this_coro::executor;

  // 创建数据库连接
  dbng<mysql_async> db(executor);

  // 连接数据库
  bool connected = co_await db.connect(
    "127.0.0.1",  // host
    "root",       // user
    "password",   // password
    "test_db",    // database
    5,            // timeout (seconds)
    3306          // port
  );

  if (!connected) {
    std::cerr << "Failed to connect: " << db.get_last_error() << std::endl;
    co_return;
  }

  // 调用数据库操作函数
  co_await database_operations(db);

  // 断开连接
  co_await db.disconnect();
}

// 主函数
int main() {
  asio::io_context ctx;

  // 启动协程
  asio::co_spawn(ctx, main_coroutine(), asio::detached);

  // 运行事件循环
  ctx.run();

  return 0;
}
```

### 2. 错误处理

```cpp
asio::awaitable<void> safe_database_operation(dbng<mysql_async>& db) {
  try {
    // 执行操作
    auto result = co_await db.query_s<Person>();

    // 检查错误
    if (db.has_error()) {
      std::cerr << "Query error: " << db.get_last_error() << std::endl;
    }

  } catch (const std::exception& e) {
    std::cerr << "Exception: " << e.what() << std::endl;
  }
}
```

## CRUD 操作

### 1. 插入数据

```cpp
asio::awaitable<void> insert_examples(dbng<mysql_async>& db) {
  // 插入单条记录
  Person p{0, "Bob", 30};
  int affected = co_await db.insert(p);

  // 获取插入的 ID
  uint64_t id = co_await db.get_insert_id_after_insert(p);
  std::cout << "Inserted ID: " << id << std::endl;

  // 批量插入
  std::vector<Person> persons = {
    {0, "Charlie", 28},
    {0, "David", 32}
  };
  affected = co_await db.insert(persons);

  // Replace 操作
  Person existing{1, "Bob Updated", 31};
  affected = co_await db.replace(existing);
}
```

### 2. 查询数据

```cpp
asio::awaitable<void> query_examples(dbng<mysql_async>& db) {
  // 查询所有记录
  auto all_persons = co_await db.query_s<Person>();

  // 带条件查询
  auto filtered = co_await db.query_s<Person>("age > ?", 25);

  // 查询单个字段（元组）
  auto ages = co_await db.query_s<std::tuple<int>>(
    "SELECT age FROM Person WHERE age > ?", 20
  );

  // 查询多个字段
  auto names_ages = co_await db.query_s<std::tuple<std::string, int>>(
    "SELECT name, age FROM Person"
  );
}
```

### 3. 更新数据

```cpp
asio::awaitable<void> update_examples(dbng<mysql_async>& db) {
  // 更新单条记录（通过主键）
  Person p{1, "Bob Updated", 31};
  int affected = co_await db.update(p);

  // 批量更新
  std::vector<Person> persons = {...};
  affected = co_await db.update(persons);

  // 更新部分字段
  affected = co_await db.update_some<&Person::age>(p);
}
```

### 4. 删除数据

```cpp
asio::awaitable<void> delete_examples(dbng<mysql_async>& db) {
  // 删除记录
  uint64_t deleted = co_await db.delete_records_s<Person>("age < ?", 18);

  std::cout << "Deleted " << deleted << " rows" << std::endl;
}
```

## 链式查询接口

### 1. 基础链式查询

```cpp
asio::awaitable<void> chain_query_basic(dbng<mysql_async>& db) {
  // 查询所有
  auto all = co_await db.select(all)
                   .from<Person>()
                   .collect();

  // 选择特定字段
  auto names = co_await db.select(col(&Person::name))
                     .from<Person>()
                     .collect();

  // 带条件查询
  auto adults = co_await db.select(all)
                      .from<Person>()
                      .where(col(&Person::age) >= 18)
                      .collect();
}
```

### 2. 复杂查询

```cpp
asio::awaitable<void> chain_query_advanced(dbng<mysql_async>& db) {
  // 排序和分页
  auto result = co_await db.select(all)
                      .from<Person>()
                      .where(col(&Person::age) > 20)
                      .order_by(col(&Person::age).desc())
                      .limit(10)
                      .offset(0)
                      .collect();

  // 聚合查询
  auto count_result = co_await db
    .select(count(), col(&Person::age))
    .from<Person>()
    .group_by(col(&Person::age))
    .having(count() > 1)
    .collect();

  // 标量查询（返回单个值）
  int max_age = co_await db.select(col(&Person::age))
                      .from<Person>()
                      .where(col(&Person::name) == "Alice")
                      .scalar();
}
```

### 3. 参数化查询

```cpp
asio::awaitable<void> parameterized_query(dbng<mysql_async>& db) {
  // 使用 token 占位符
  std::string name = "Alice";
  auto result = co_await db.select(all)
                      .from<Person>()
                      .where(col(&Person::name).param())
                      .collect(name);  // 传入参数

  // 多个参数
  int min_age = 20;
  int max_age = 30;
  auto range_result = co_await db.select(all)
                            .from<Person>()
                            .where(col(&Person::age).between(token, token))
                            .collect(min_age, max_age);
}
```

### 4. JOIN 查询

```cpp
struct Department {
  int id;
  std::string name;
  int manager_id;
};
REGISTER_AUTO_KEY(Department, id)
YLT_REFL(Department, id, name, manager_id)

asio::awaitable<void> join_query(dbng<mysql_async>& db) {
  // INNER JOIN
  auto result = co_await db
    .select(col(&Person::name), col(&Department::name))
    .from<Person>()
    .inner_join(col(&Person::id), col(&Department::manager_id))
    .collect();

  // LEFT JOIN
  auto left_result = co_await db
    .select(col(&Person::name), col(&Department::name))
    .from<Person>()
    .left_join(col(&Person::id), col(&Department::manager_id))
    .where(col(&Person::age) > 25)
    .collect();
}
```

## 链式更新和删除

### 1. 链式更新

```cpp
asio::awaitable<void> chain_update(dbng<mysql_async>& db) {
  // 更新单个字段
  int affected = co_await db.update<Person>()
                       .set(col(&Person::age), 26)
                       .where(col(&Person::name) == "Alice")
                       .execute();

  // 更新多个字段
  affected = co_await db.update<Person>()
                   .set(col(&Person::name), "Alice Smith")
                   .set(col(&Person::age), 27)
                   .where(col(&Person::id) == 1)
                   .execute();

  // 设置为 NULL
  affected = co_await db.update<Person>()
                   .set_null(col(&Person::name))
                   .where(col(&Person::id) == 2)
                   .execute();

  // 更新所有记录（无 where 条件）
  affected = co_await db.update<Person>()
                   .set(col(&Person::age), 30)
                   .execute_all();
}
```

### 2. 链式删除

```cpp
asio::awaitable<void> chain_delete(dbng<mysql_async>& db) {
  // 条件删除
  int affected = co_await db.remove<Person>()
                       .where(col(&Person::age) < 18)
                       .execute();

  // 复杂条件
  affected = co_await db.remove<Person>()
                   .where(col(&Person::age) < 18 ||
                          col(&Person::name).like("Test%"))
                   .execute();

  // 删除所有记录（危险操作！）
  affected = co_await db.remove<Person>()
                   .execute_all();
}
```

## DDL 操作

### 1. 创建表

```cpp
asio::awaitable<void> create_table_examples(dbng<mysql_async>& db) {
  // 简单创建表
  bool success = co_await db.create_datatable<Person>(
    ormpp_auto_key{"id"}
  );

  // 使用链式接口创建表
  success = co_await db.create_table<Person>()
              .auto_increment(col(&Person::id))
              .not_null(col(&Person::name))
              .unique(col(&Person::name))
              .default_value(col(&Person::age), 18)
              .charset("utf8mb4")
              .engine("InnoDB")
              .execute();
}
```

### 2. 修改表

```cpp
asio::awaitable<void> alter_table_examples(dbng<mysql_async>& db) {
  // 添加列
  bool success = co_await db.alter_table<Person>()
                       .add_column("email", "VARCHAR(255)")
                       .execute();

  // 删除列
  success = co_await db.alter_table<Person>()
                  .drop_column("email")
                  .execute();

  // 修改列类型
  success = co_await db.alter_table<Person>()
                  .modify_column(col(&Person::name), "VARCHAR(100)")
                  .execute();

  // 添加索引
  success = co_await db.alter_table<Person>()
                  .add_index("idx_age", col(&Person::age))
                  .execute();
}
```

## 事务处理

```cpp
asio::awaitable<void> transaction_example(dbng<mysql_async>& db) {
  try {
    // 开始事务
    if (!co_await db.begin()) {
      throw std::runtime_error("Failed to begin transaction");
    }

    // 执行多个操作
    Person p1{0, "Alice", 25};
    co_await db.insert(p1);

    Person p2{0, "Bob", 30};
    co_await db.insert(p2);

    // 提交事务
    if (!co_await db.commit()) {
      throw std::runtime_error("Failed to commit transaction");
    }

    std::cout << "Transaction committed successfully" << std::endl;

  } catch (const std::exception& e) {
    // 回滚事务
    co_await db.rollback();
    std::cerr << "Transaction rolled back: " << e.what() << std::endl;
  }
}
```

## 高级特性

### 1. 可选类型支持

```cpp
struct OptionalPerson {
  int id;
  std::optional<std::string> name;
  std::optional<int> age;
};
REGISTER_AUTO_KEY(OptionalPerson, id)
YLT_REFL(OptionalPerson, id, name, age)

asio::awaitable<void> optional_example(dbng<mysql_async>& db) {
  // 插入带 NULL 值的记录
  OptionalPerson p{0, std::nullopt, std::optional<int>{25}};
  co_await db.insert(p);

  // 查询
  auto persons = co_await db.query_s<OptionalPerson>();
  for (const auto& person : persons) {
    if (person.name.has_value()) {
      std::cout << "Name: " << *person.name << std::endl;
    } else {
      std::cout << "Name is NULL" << std::endl;
    }
  }
}
```

### 2. 枚举类型支持

```cpp
enum class Gender { Male = 0, Female = 1 };

struct PersonWithEnum {
  int id;
  std::string name;
  Gender gender;
};
REGISTER_AUTO_KEY(PersonWithEnum, id)
YLT_REFL(PersonWithEnum, id, name, gender)

asio::awaitable<void> enum_example(dbng<mysql_async>& db) {
  PersonWithEnum p{0, "Alice", Gender::Female};
  co_await db.insert(p);

  auto persons = co_await db.query_s<PersonWithEnum>();
  for (const auto& person : persons) {
    std::cout << person.name << " is "
              << (person.gender == Gender::Male ? "Male" : "Female")
              << std::endl;
  }
}
```

### 3. BLOB 数据支持

```cpp
struct BlobData {
  int id;
  ormpp::blob data;
};
YLT_REFL(BlobData, id, data)

asio::awaitable<void> blob_example(dbng<mysql_async>& db) {
  // 插入二进制数据
  BlobData bd{1};
  const char* binary_data = "Binary\0Data\0With\0Nulls";
  bd.data.assign(binary_data, binary_data + 23);

  co_await db.insert(bd);

  // 查询二进制数据
  auto results = co_await db.query_s<BlobData>("id = ?", 1);
  if (!results.empty()) {
    std::cout << "Blob size: " << results[0].data.size() << std::endl;
  }
}
```

## 并发操作

### 1. 多个并发查询

```cpp
asio::awaitable<void> concurrent_queries() {
  auto executor = co_await asio::this_coro::executor;

  // 创建多个数据库连接
  dbng<mysql_async> db1(executor);
  dbng<mysql_async> db2(executor);

  co_await db1.connect(...);
  co_await db2.connect(...);

  // 并发执行查询
  auto [result1, result2] = co_await std::tuple{
    db1.query_s<Person>("age > ?", 20),
    db2.query_s<Person>("age < ?", 30)
  };

  std::cout << "Query 1: " << result1.size() << " rows" << std::endl;
  std::cout << "Query 2: " << result2.size() << " rows" << std::endl;
}
```

### 2. 连接池模式

```cpp
class ConnectionPool {
  asio::io_context& ctx_;
  std::vector<std::unique_ptr<dbng<mysql_async>>> connections_;

public:
  ConnectionPool(asio::io_context& ctx, size_t pool_size) : ctx_(ctx) {
    for (size_t i = 0; i < pool_size; ++i) {
      connections_.push_back(
        std::make_unique<dbng<mysql_async>>(ctx_.get_executor())
      );
    }
  }

  asio::awaitable<void> init(const std::string& host,
                             const std::string& user,
                             const std::string& pass,
                             const std::string& db) {
    for (auto& conn : connections_) {
      co_await conn->connect(host, user, pass, db);
    }
  }

  dbng<mysql_async>* get_connection(size_t index) {
    return connections_[index % connections_.size()].get();
  }
};
```

## 性能优化建议

### 1. 批量操作

```cpp
// ❌ 不推荐：逐条插入
for (const auto& person : persons) {
  co_await db.insert(person);
}

// ✅ 推荐：批量插入
co_await db.insert(persons);
```

### 2. 启用事务

```cpp
// 对于批量操作，启用事务可以提高性能
db.set_enable_transaction(true);
co_await db.insert(large_vector);
```

### 3. 使用预编译语句（参数化查询）

```cpp
// ✅ 推荐：使用参数化查询
auto result = co_await db.query_s<Person>("name = ?", name);

// ❌ 不推荐：字符串拼接（有 SQL 注入风险）
auto result = co_await db.query_s<Person>("name = '" + name + "'");
```

## 常见问题

### 1. 如何处理连接断开？

```cpp
asio::awaitable<bool> reconnect_with_retry(dbng<mysql_async>& db,
                                           const std::string& host,
                                           const std::string& user,
                                           const std::string& pass,
                                           const std::string& database) {
  auto executor = co_await asio::this_coro::executor;

  while (true) {
    if (co_await db.connect(host, user, pass, database)) {
      co_return true;
    }

    std::cerr << "Connection failed, retrying..." << std::endl;
    asio::steady_timer timer(executor);
    timer.expires_after(std::chrono::seconds(5));
    co_await timer.async_wait(asio::use_awaitable);
  }
}
```

### 2. 如何获取详细错误信息？

```cpp
if (db.has_error()) {
  std::cerr << "Error: " << db.get_last_error() << std::endl;
}

// 获取影响的行数
int affected = db.get_last_affect_rows();
```

### 3. 如何在非协程函数中使用？

```cpp
void non_coroutine_function() {
  asio::io_context ctx;

  // 使用 co_spawn 启动协程
  auto future = asio::co_spawn(
    ctx,
    database_operations(),
    asio::use_future
  );

  // 运行事件循环
  ctx.run();

  // 等待结果
  try {
    future.get();
  } catch (const std::exception& e) {
    std::cerr << "Error: " << e.what() << std::endl;
  }
}
```

## 完整示例程序

```cpp
#include <asio.hpp>
#include <iostream>
#include "dbng.hpp"
#include "mysql_async.hpp"

using namespace ormpp;

struct User {
  int id;
  std::string username;
  std::string email;
  int age;
};
REGISTER_AUTO_KEY(User, id)
YLT_REFL(User, id, username, email, age)

// 用户管理函数（接受 db 作为参数）
asio::awaitable<void> user_management(dbng<mysql_async>& db) {
  // 创建表
  co_await db.execute("DROP TABLE IF EXISTS User");
  co_await db.create_table<User>()
      .auto_increment(col(&User::id))
      .not_null(col(&User::username), col(&User::email))
      .unique(col(&User::username))
      .execute();

  // 插入用户
  std::vector<User> users = {
    {0, "alice", "alice@example.com", 25},
    {0, "bob", "bob@example.com", 30},
    {0, "charlie", "charlie@example.com", 28}
  };
  co_await db.insert(users);

  // 查询所有用户
  auto all_users = co_await db.select(all).from<User>().collect();
  std::cout << "All users:" << std::endl;
  for (const auto& user : all_users) {
    std::cout << "  " << user.username << " (" << user.age << ")" << std::endl;
  }

  // 查询年龄大于 26 的用户
  auto filtered = co_await db.select(all)
                        .from<User>()
                        .where(col(&User::age) > 26)
                        .order_by(col(&User::age).desc())
                        .collect();

  std::cout << "\nUsers older than 26:" << std::endl;
  for (const auto& user : filtered) {
    std::cout << "  " << user.username << " (" << user.age << ")" << std::endl;
  }

  // 更新用户
  int affected = co_await db.update<User>()
                       .set(col(&User::age), 31)
                       .where(col(&User::username) == "bob")
                       .execute();
  std::cout << "\nUpdated " << affected << " users" << std::endl;

  // 删除用户
  affected = co_await db.remove<User>()
                   .where(col(&User::age) < 27)
                   .execute();
  std::cout << "Deleted " << affected << " users" << std::endl;

  // 最终结果
  auto final_users = co_await db.query_s<User>();
  std::cout << "\nFinal users:" << std::endl;
  for (const auto& user : final_users) {
    std::cout << "  " << user.username << " (" << user.age << ")" << std::endl;
  }
}

// 主协程（只在这里获取 executor 和创建连接）
asio::awaitable<void> main_coroutine() {
  // 获取 executor
  auto executor = co_await asio::this_coro::executor;

  // 创建数据库连接
  dbng<mysql_async> db(executor);

  // 连接数据库
  if (!co_await db.connect("localhost", "root", "password", "testdb")) {
    std::cerr << "Failed to connect" << std::endl;
    co_return;
  }

  // 调用用户管理函数
  co_await user_management(db);

  co_await db.disconnect();
}

int main() {
  try {
    asio::io_context ctx;
    asio::co_spawn(ctx, user_management(), asio::detached);
    ctx.run();
  } catch (const std::exception& e) {
    std::cerr << "Exception: " << e.what() << std::endl;
    return 1;
  }
  return 0;
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

ORMPP 的异步接口通过 Asio 协程提供了：

1. ✅ **简洁的 API** - 使用 `co_await` 编写异步代码如同同步代码
2. ✅ **完整的功能** - 支持所有 CRUD 操作和链式查询
3. ✅ **类型安全** - 编译期类型检查
4. ✅ **高性能** - 基于 Asio 的高效异步 I/O
5. ✅ **易于使用** - 统一的接口，无需学习复杂的回调

通过本指南，你应该能够熟练使用 ORMPP 的异步接口进行数据库编程了！
