# 异步 PostgreSQL

`ormpp::postgresql_async` 使用 libpq 的非阻塞连接、发送和结果读取接口，返回
`asio::awaitable<T>`。接口与 `mysql_async` 一致，支持 `dbng`、查询构造器和
`async_connection_pool`，需要 C++20、standalone Asio（CI 使用 1.36）和 libpq。

## 编译和运行

```sh
sudo apt-get install libpq-dev libasio-dev
cmake -S . -B build -DENABLE_PG_ASYNC=ON
cmake --build build --target ormpp_pg_async_example test_postgresql_async
PGHOST=127.0.0.1 PGUSER=root PGPASSWORD=123456 PGDATABASE=test_ormppdb \
  ./build/example/ormpp_pg_async_example
```

Asio 安装在其他位置时，添加 `-DORMPP_ASIO_INCLUDE_DIR=/path/to/asio/include`。
直接包含头文件的项目需要定义 `ORMPP_ENABLE_PG_ASYNC`，添加 ormpp、Asio 和
libpq 的头文件路径，并链接 `pq` 和平台线程库。`ENABLE_PG` 与 `ENABLE_PG_ASYNC`
独立，可同时启用。

完整示例位于 [example/postgresql_async.cpp](../example/postgresql_async.cpp)。

```cpp
#include <asio.hpp>
#include "dbng.hpp"
#include "postgresql_async.hpp"

struct async_employee {
  int id;
  std::string name;
  int age;
};
REGISTER_AUTO_KEY(async_employee, id)
YLT_REFL(async_employee, id, name, age)

asio::awaitable<void> work() {
  ormpp::dbng<ormpp::postgresql_async> db(co_await asio::this_coro::executor);
  if (!(co_await db.connect("localhost", "root", "123456", "test_ormppdb", 5, 5432))) {
    throw std::runtime_error(db.get_last_error());
  }
  co_await db.create_datatable<async_employee>();
  auto id = co_await db.get_insert_id_after_insert(async_employee{0, "alice", 20});
  auto rows = co_await db.query_s<async_employee>("id=$1", id);
  auto selected = co_await db.select(ormpp::all).from<async_employee>()
      .where(ormpp::col(&async_employee::name).param()).collect("alice");
  co_await db.disconnect();
}
```

`connect` 的参数顺序为 host、user、password、database、超时秒数、port，也接受
同顺序的 tuple。host 必须是单个非空的主机名、IP 或 Unix socket 目录；TCP 主机名
由 Asio 异步解析后通过 `hostaddr` 交给 libpq，保留原主机名用于 TLS 验证。
默认端口为 5432。TLS 由 libpq 管理，可通过 `PGSSLMODE=require` 或
`PGSSLMODE=verify-full PGSSLROOTCERT=/path/to/ca.crt` 配置。

## 参数和结果

手写 SQL 使用 PostgreSQL 的 `$1`、`$2` 占位符；查询构造器会自动生成它们。
实体和字段名沿用现有 PostgreSQL SQL 生成器的普通标识符规则（不加双引号，名称由
PostgreSQL 转为小写；避免使用保留字）。值通过 `PQsendQueryParams` 绑定，不会拼接进 SQL。文本中的 NUL 会报告错误，二进制
数据使用 `ormpp::blob`，映射为 `bytea`。支持可空字段、枚举、布尔值、定长字符数组、
日期时间和 decimal，以及 tuple 和包含反射实体的 tuple 查询结果。

`insert`、`replace`、`update` 返回受影响行数，失败返回 `std::numeric_limits<int>::min()`。
`delete_records_s` 返回删除行数，失败返回 0；`execute`、连接和事务接口返回 bool；
`query_s` 失败返回空 vector。用 `has_error()` 和 `get_last_error()` 区分错误与空结果。
每次操作开始时清空上一条错误。

`get_insert_id_after_insert` 通过注册自增字段的 `RETURNING` 获取 ID，失败返回 0。
vector 重载返回最后一个插入 ID。批量写入默认在一次事务中完成，失败时回滚并保留
原始错误；`set_enable_transaction(false)` 可关闭自动批量事务。

普通结果拥有自己的数据。`std::string_view` 指向连接保存的结果存储，只在该连接
下一次 `query_s` 或销毁前有效；需要长期保存或并发消费时使用 `std::string`。

## 调度、取消和连接池

每个连接都有独立的 Asio strand，并将整个操作（包括批量事务）串行执行。
同一连接的操作可以从共享 `io_context` 的多个线程提交。参数在返回 awaitable 前
复制；已创建的操作持有连接状态，操作执行期间销毁公开的连接对象不会关闭正在使用
的 socket。executor 和 `io_context` 必须活到操作及清理完成；推荐显式等待 `disconnect()`。

`disconnect()` 等待前面的操作结束。需要中止正在等待的 I/O 时，使用 Asio 的取消
机制；取消会关闭当前连接，下一次查询前需重新 `connect()`。取消信号应在
`get_executor()` 返回的连接 strand 上发出。等待串行队列时取消不会中止前面的操作。
SQL 错误会先读完协议结果，因此普通 SQL 错误后可以继续使用连接。

显式 `begin()` / `commit()` 之间需要由调用方独占连接；逐个操作的串行化不提供
跨调用的事务隔离。可以用池租约持有整个事务：

```cpp
auto executor = co_await asio::this_coro::executor;
auto pool = std::make_shared<ormpp::async_connection_pool<ormpp::postgresql_async>>(executor);
if (co_await pool->init(4, "127.0.0.1", "root", "123456", "test_ormppdb", 5, 5432)) {
  {
    auto connection = co_await pool->get();
    if (connection) {
      co_await connection->begin();
      // 在此使用 connection 完成事务。
      co_await connection->commit();
    } // 租约归还连接。
  }
  co_await pool->close_all();
}
```

沿用现有池的使用方式：在同一串行 executor 上管理池和租约；每个数据库连接内部
仍使用自己的 strand。`async_simple::coro::Lazy` 可通过现有的
`ormpp::adapter::from_asio(operation, executor)` 桥接，约束见
[协程适配器说明](async_simple_ormpp_coroutine.md)。

暂不提供 COPY、pipeline、流式逐行结果或服务端 prepared statement 管理。
COPY 请求会关闭连接并返回错误，避免残留协议数据影响后续查询。

实现遵循 PostgreSQL 官方的[异步命令接口](https://www.postgresql.org/docs/current/libpq-async.html)
和[非阻塞连接接口](https://www.postgresql.org/docs/current/libpq-connect.html)。

## 测试

```sh
ORMPP_ASYNC_PG_HOST=localhost ORMPP_ASYNC_PG_USER=root \
ORMPP_ASYNC_PG_PASSWORD=123456 ORMPP_ASYNC_PG_DB=test_ormppdb \
ORMPP_ASYNC_PG_PORT=5432 PGSSLMODE=disable \
  ctest --test-dir build -R '^test_postgresql_async$' -V
```

测试需要可连接且允许创建测试表的数据库；连接失败会令测试失败。
CI 分别运行 GCC/Clang 的 Debug/Release、Clang ASan 和 Clang TSan，并在每种配置下
覆盖明文、TLS 和证书校验连接。并发测试通过观察 `pg_locks` 中未获准的 advisory lock
确认查询已挂起，再执行关闭、销毁或取消，覆盖单线程与四线程共享 executor。
