# 在 async_simple 协程中调用 ormpp 异步协程

ormpp 1.36 的 `mysql_async` 基于 Asio 协程实现，数据库接口返回：

```cpp
asio::awaitable<T, asio::any_io_executor>
```

项目里的业务协程使用 cinatra/ylt 带的 `async_simple::coro::Lazy<T>`。这两套协程不能直接混用，因为 `asio::awaitable` 需要由 Asio 协程调度器启动，而 `async_simple::Lazy` 有自己的 executor 和恢复机制。

因此，在 `async_simple` 协程中调用 ormpp 异步数据库接口时，需要一层桥接：

```text
async_simple::Lazy
  -> co_await 自定义 awaiter
  -> awaiter 用 asio::co_spawn 启动 ormpp 的 asio::awaitable
  -> ormpp/mysql_async 完成数据库 I/O
  -> 保存结果或异常
  -> 在同一个 asio executor 上恢复原业务协程
```

## 桥接入口

项目里的桥接入口是 `adapter::from_asio`：

```cpp
template <typename T>
inline auto from_asio(
    asio::awaitable<T, asio::any_io_executor> awaitable,
    asio::any_io_executor executor) {
  return AsioAwaitableAdapter<T>(std::move(awaitable), executor);
}
```

调用方式：

```cpp
auto rows = co_await adapter::from_asio(
    db.select(ormpp::all).from<chat_message_t>().collect(),
    asio_executor);
```

实际业务里不要每次手写 executor，建议通过 `mysql_async_session` 封装。

## 轻量桥接设计

`AsioAwaitableAdapter` 不是普通函数包装器，它实现的是 async_simple 能识别的
awaiter 接口。当前实现按项目的执行模型走轻量路径：Asio executor 和
async_simple executor 指向同一个底层 `io_context`，并且这个 executor 只由一个线程
`run()`。

在这个模型下，桥接对象不需要额外的 `SharedState`、`shared_ptr`、mutex 或原子变量。
结果、异常和 continuation 直接保存在 awaiter 对象里：

```cpp
asio::awaitable<T, asio::any_io_executor> awaitable_;
asio::any_io_executor asio_executor_;
std::optional<stored_type> result_;
std::exception_ptr exception_;
std::coroutine_handle<> resume_handle_;
```

关键点是 `asio::co_spawn` 不在构造函数里启动，而是在 `await_suspend()` 之后启动。
这样 awaiter 已经被 async_simple 移动到父协程帧里，Asio 侧协程可以安全通过
`this` 写入结果并在完成后恢复父协程。

## Awaiter 方法

```cpp
auto coAwait(async_simple::Executor* executor) noexcept {
  return std::move(*this);
}
```

`coAwait()` 是 async_simple 的扩展点。业务协程执行 `co_await adapter` 时，
async_simple 会调用这个方法并把返回的 awaiter 放进父协程帧。当前实现不需要保存
`executor` 指针，因为恢复动作直接投递到传入的 `asio_executor_`；前提是这个
`asio_executor_` 和当前 async_simple executor 是同一个执行上下文。

```cpp
bool await_ready() const noexcept {
  return false;
}
```

`await_ready()` 固定返回 `false`。Asio 协程只在 `await_suspend()` 之后启动，
不存在“构造后、挂起前已经完成”的顺序，因此不需要 `completed` 状态来兜底。

```cpp
bool await_suspend(std::coroutine_handle<> handle) noexcept {
  resume_handle_ = handle;
  asio::post(asio_executor_, [this]() {
    start_asio();
  });
  return true;
}
```

`await_suspend()` 保存当前 async_simple 协程的 continuation，然后用
`asio::post()` 延迟启动 Asio 协程。这里用 `post` 而不是 `dispatch`，是为了避免同线程
executor 直接内联执行，导致 Asio 协程同步完成并在 `await_suspend()` 返回前恢复父协程。

```cpp
T await_resume() {
  if (exception_) {
    std::rethrow_exception(exception_);
  }

  if constexpr (std::is_void_v<T>) {
    return;
  } else {
    return std::move(*result_);
  }
}
```

`await_resume()` 是业务协程恢复后调用的最后一步。它不再关心 asio 的执行细节，
只做两件事：有异常就重抛，没有异常就返回 `result`。

这意味着这个桥不是“把一个 awaitable 包起来”这么简单，而是在做一次状态交接：

```text
async_simple::Lazy
  -> 保存 continuation
  -> post 启动 asio::awaitable
  -> asio 完成后写入 result / exception
  -> post 恢复 async_simple 协程
```

## Asio 到 async_simple 的恢复

桥接对象在 `await_suspend()` 后，用 `asio::co_spawn` 启动 ormpp 返回的
`asio::awaitable`：

```cpp
asio::co_spawn(asio_executor_, run_asio(), asio::detached);
```

`run_asio()` 等待真正的数据库 I/O，把返回值写入 `result_`，或者把异常写入
`exception_`。完成后不直接调用 `resume_handle_.resume()`，而是再次用
`asio::post()` 投递恢复动作：

```cpp
asio::post(asio_executor_, [handle = resume_handle_]() mutable {
  handle.resume();
});
```

这样做有两个目的：

1. 保证恢复发生在后续事件中，避免在 Asio 桥接协程尚未退出时销毁 awaiter。
2. 在同一个 `io_context` 单线程执行时，`result_`、`exception_` 和 `resume_handle_`
   不会被并发访问。

## 封装 mysql_async_session

为了让业务代码保持简单，项目封装了 `mysql_async_session`：

```cpp
class mysql_async_session {
 public:
  using db_type = ormpp::dbng<ormpp::mysql_async>;

  mysql_async_session()
      : executor_(coro_io::get_global_executor()->get_asio_executor()),
        db_(executor_) {}

  db_type &raw() { return db_; }

  template <typename T>
  auto await(asio::awaitable<T, asio::any_io_executor> awaitable) {
    return adapter::from_asio(std::move(awaitable), executor_);
  }

  async_simple::coro::Lazy<bool> connect(
      const std::string &host,
      const std::string &user,
      const std::string &passwd,
      const std::string &database,
      const std::optional<int> &timeout = {},
      const std::optional<int> &port = {}) {
    co_return co_await await(
        db_.connect(host, user, passwd, database, timeout, port));
  }

 private:
  asio::any_io_executor executor_;
  db_type db_;
};
```

这样业务侧看到的是 `async_simple::Lazy`，而底层数据库 I/O 仍然由 Asio 非阻塞执行。

如果想把数据库会话绑定到当前请求的 executor，也可以显式构造：

```cpp
mysql_async_session db(req.get_conn()->get_executor()->get_asio_executor());
```

这点很重要：传给 `from_asio()` 的 `asio_executor` 必须和当前 async_simple 协程使用的
executor 指向同一个单线程执行上下文。

## 使用 ormpp 链式接口

ormpp 的链式查询接口可以继续使用，只需要把最终的 `.collect()` 结果交给 `db.await(...)`：

```cpp
async_simple::coro::Lazy<std::vector<chat_message_t>>
load_messages(mysql_async_session &db, uint64_t channel_id) {
  auto rows = co_await db.await(
      db.raw()
          .select(ormpp::all)
          .from<chat_message_t>()
          .where(col(&chat_message_t::channel_id) == channel_id)
          .order_by(col(&chat_message_t::id).asc())
          .collect());

  co_return rows;
}
```

这条链路是非阻塞的：

```text
select().from().where().collect()
  -> 返回 asio::awaitable<std::vector<T>>
  -> db.await(...) 转成 async_simple 可等待对象
  -> co_await 挂起当前业务协程
  -> 数据库 I/O 完成后恢复业务协程
```

## 完整示例

```cpp
async_simple::coro::Lazy<void> run() {
  mysql_async_session db;

  if (!(co_await db.connect("127.0.0.1", "root", "pass", "test", 5, 3306))) {
    co_return;
  }

  auto messages = co_await db.await(
      db.raw()
          .select(ormpp::all)
          .from<chat_message_t>()
          .limit(20)
          .collect());

  for (auto &msg : messages) {
    // process msg
  }
}
```

## 错误处理

`mysql_async` 的大多数接口把数据库错误记录在连接对象里，并通过返回值表示失败；
桥接层本身也会把 asio 协程里抛出的异常保存到 `exception_`，再在
`await_resume()` 中重抛。

业务代码建议同时处理这两类失败：

```cpp
async_simple::coro::Lazy<void> query_with_error_handling(mysql_async_session &db) {
  if (!(co_await db.connect("127.0.0.1", "root", "pass", "test", 5, 3306))) {
    auto err = db.get_last_error();
    co_return;
  }

  try {
    auto rows = co_await db.await(
        db.raw().select(ormpp::all).from<chat_message_t>().collect());
  } catch (const std::exception &e) {
    // 桥接层或底层 asio 协程抛出的异常会到这里。
    co_return;
  }

  if (db.has_error()) {
    auto err = db.get_last_error();
  }
}
```

事务处理也应保持在协程链里，不要在中间阻塞：

```cpp
async_simple::coro::Lazy<bool> save_two_rows(mysql_async_session &db,
                                             const row_t &a,
                                             const row_t &b) {
  if (!(co_await db.await(db.raw().begin()))) {
    co_return false;
  }

  try {
    if ((co_await db.await(db.raw().insert(a))) != 1 ||
        (co_await db.await(db.raw().insert(b))) != 1) {
      co_await db.await(db.raw().rollback());
      co_return false;
    }

    co_return co_await db.await(db.raw().commit());
  } catch (...) {
    co_await db.await(db.raw().rollback());
    throw;
  }
}
```

## 性能和线程模型

当前 adapter 是为“一个线程 run 一个 executor”的模型写的。它没有给每次桥接加
mutex，也没有使用 CAS；每次桥接的主要额外成本是：

1. 一个 awaiter 对象保存在父 async_simple 协程帧里。
2. 一次 `asio::co_spawn()` 启动数据库 awaitable。
3. 两次 `asio::post()`：一次延迟启动 Asio 协程，一次延迟恢复业务协程。

相对于一次 MySQL 网络 I/O，这个开销通常可以忽略。真正需要关注的是线程模型：
如果同一个 executor 将来被多个线程同时 `run()`，当前普通字段就不再安全。
那时要么把数据库桥接固定到 strand / 单线程 executor，要么恢复成带独立共享状态和
同步保护的多线程版本。

还有一个生命周期前提：父 async_simple 协程在等待这个 adapter 时不能被外部取消或销毁。
当前实现依赖 awaiter 留在父协程帧里，Asio 桥接协程通过 `this` 写入结果。如果后续要支持
取消、超时主动销毁父协程、或者 detached 后不等待结果，就不能再用这个裸 awaiter fast path，
需要让 Asio 侧持有一份独立状态。

## 注意事项

不要在 `async_simple::Lazy` 业务协程里直接 `co_await` ormpp 返回的 `asio::awaitable`。正确做法是通过桥接层启动 Asio 协程，再恢复 `async_simple` 协程。

不要在协程链内部使用 `syncAwait` 或其他阻塞等待。顶层入口可以有同步边界，但业务协程内部应保持 `co_await`。

链式接口保留在 `raw()` 上，最终异步结果统一通过 `await()` 桥接：

```cpp
co_await db.await(db.raw().select(...).from<T>().collect());
```

这样可以同时保留 ormpp 的链式表达能力和 async_simple 的业务协程风格。
