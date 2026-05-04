# ormpp 异步协程接口的设计与实现

## 目录

1. [背景与动机](#背景与动机)
2. [整体架构](#整体架构)
3. [第一层：mysql_async - MySQL 协议实现](#第一层mysql_async---mysql-协议实现)
4. [第二层：协程框架桥接](#第二层协程框架桥接)
5. [第三层：链式查询 API](#第三层链式查询-api)
6. [第四层：连接池](#第四层连接池)
7. [性能分析](#性能分析)
8. [使用指南](#使用指南)
9. [设计边界与限制](#设计边界与限制)

---

## 背景与动机

### 问题

ormpp 原有接口基于同步阻塞调用。在高并发异步服务（如 cinatra/ylt 构建的 HTTP 服务）中：

- **阻塞数据库调用占用线程**：每个查询阻塞一个线程，限制并发能力
- **抵消协程模型优势**：协程的轻量级并发被阻塞 I/O 抵消
- **资源利用率低**：大量线程等待 I/O，CPU 和内存浪费

### 目标

设计一套完全异步的数据库接口，使得：

1. **数据库 I/O 和业务逻辑运行在同一个非阻塞事件循环**
2. **保留 ormpp 的类型安全和链式查询 API**
3. **与 async_simple 协程框架无缝集成**
4. **零依赖第三方 MySQL 客户端库**

---

## 整体架构

```
┌─────────────────────────────────────────────────────────────┐
│  业务代码 (async_simple::Lazy)                               │
│  co_await db.await(db.raw().select(...).from<T>().collect())│
└────────────────────────┬────────────────────────────────────┘
                         │ co_await
                         ▼
┌─────────────────────────────────────────────────────────────┐
│  AsioAwaitableAdapter / SafeAsioAwaitableAdapter            │
│  协程框架桥接层                                              │
└────────────────────────┬────────────────────────────────────┘
                         │ asio::co_spawn
                         ▼
┌─────────────────────────────────────────────────────────────┐
│  dbng<mysql_async>                                          │
│  异步数据库接口 (ASIO 协程)                                  │
│  - select().from<T>().where(...).collect()                  │
│  - insert() / update() / delete()                           │
│  - execute() / begin() / commit() / rollback()              │
└────────────────────────┬────────────────────────────────────┘
                         │ asio::async_read/write
                         ▼
┌─────────────────────────────────────────────────────────────┐
│  MySQL TCP 连接                                              │
│  完整实现 MySQL 客户端协议                                   │
└─────────────────────────────────────────────────────────────┘
```

### 四层架构

| 层次 | 组件 | 职责 |
|---|---|---|
| **第一层** | `mysql_async` | 直接实现 MySQL 客户端协议，所有 I/O 用 ASIO 协程完成 |
| **第二层** | `AsioAwaitableAdapter` / `SafeAsioAwaitableAdapter` | 将 `asio::awaitable` 桥接为 `async_simple::Lazy` 能 `co_await` 的 awaiter |
| **第三层** | `dbng<mysql_async>` + 链式查询 | 在适配器之上提供统一的链式查询和事务 API |
| **第四层** | `async_connection_pool` | 连接池管理，支持健康检查、动态扩容、超时等待 |

---

## 第一层：mysql_async - MySQL 协议实现

### 为什么不用 libmysqlclient

官方 C 客户端是同步 API，异步化它需要：
- 额外的线程池来运行阻塞调用
- 线程间通信和同步开销
- 失去协程单线程模型的优势

`mysql_async` 直接在 ASIO 的 TCP socket 上实现 MySQL 协议，所有 I/O 都是真正的非阻塞操作。

### 类结构

```cpp
class mysql_async {
 public:
  using executor_type = asio::any_io_executor;
  template <typename T>
  using awaitable = asio::awaitable<T, executor_type>;

  static constexpr DBType db_type_v = DBType::mysql;

  explicit mysql_async(executor_type executor);
  explicit mysql_async(asio::io_context& ctx);

  // 连接管理
  awaitable<bool> connect(host, user, passwd, db, timeout, port);
  awaitable<bool> disconnect();
  awaitable<bool> ping();

  // DDL
  template <typename T, typename... Args>
  awaitable<bool> create_datatable(Args&&... args);

  // DML
  template <typename T>
  awaitable<int> insert(const T& t, ...);
  template <typename T>
  awaitable<int> update(const T& t, ...);
  template <typename T>
  awaitable<bool> delete_records(...);

  // 查询
  template <typename T>
  awaitable<std::vector<T>> query_s(Args&&... args);

  // 链式查询入口
  auto select(...);
  auto select_all();

  // 事务
  awaitable<bool> begin();
  awaitable<bool> commit();
  awaitable<bool> rollback();

  // 原始 SQL
  awaitable<bool> execute(const std::string& sql);

  // 状态查询
  bool has_error() const;
  std::string get_last_error() const;
  int get_last_affect_rows() const;
};
```

### 连接与握手流程

MySQL 连接建立需要完整的握手流程：

```
客户端                                    服务端
  │                                         │
  │  1. TCP connect                         │
  │────────────────────────────────────────▶│
  │                                         │
  │  2. read server handshake               │
  │◀────────────────────────────────────────│
  │  - protocol version                     │
  │  - server version                       │
  │  - connection id                        │
  │  - auth plugin name                     │
  │  - scramble (20 bytes)                  │
  │  - server capabilities                  │
  │                                         │
  │  3. write handshake response            │
  │────────────────────────────────────────▶│
  │  - client capabilities                  │
  │  - max packet size                      │
  │  - character set                        │
  │  - username                             │
  │  - auth response                        │
  │  - database name                        │
  │  - auth plugin name                     │
  │  - connection attributes                │
  │                                         │
  │  4. read auth response                  │
  │◀────────────────────────────────────────│
  │  - OK packet / Error / Auth Switch     │
  │                                         │
  │  5. handle auth response (可能多轮)     │
  │◀───────────────────────────────────────▶│
  │                                         │
  │  6. connected                           │
```

#### 实现细节

**1. 能力协商**

```cpp
awaitable<bool> connect(...) {
  // 客户端支持的能力
  std::uint32_t client_caps =
      client_long_password |
      client_found_rows |
      client_long_flag |
      client_protocol_41 |
      client_transactions |
      client_secure_connection |
      client_multi_statements |
      client_multi_results |
      client_plugin_auth |
      client_plugin_auth_lenenc_data |
      client_connect_attrs |
      client_deprecate_eof;

  if (!database_.empty()) {
    client_caps |= client_connect_with_db;
  }

  // 与服务端取交集
  negotiated_capabilities_ = client_caps & server_capabilities_;
}
```

**2. 认证支持**

支持两种认证插件：

- **`mysql_native_password`**：SHA1 哈希 + XOR
  ```cpp
  bytes auth_native_password(password, scramble) {
    auto hash1 = sha1(password);
    auto hash2 = sha1(hash1);
    auto hash3 = sha1(scramble + hash2);
    return hash1 XOR hash3;
  }
  ```

- **`caching_sha2_password`**：SHA256 哈希 + RSA 加密（如果需要）
  ```cpp
  bytes auth_caching_sha2(password, scramble) {
    auto hash1 = sha256(password);
    auto hash2 = sha256(hash1);
    auto hash3 = sha256(hash2 + scramble);
    return hash1 XOR hash3;
  }
  ```

**3. 认证流程处理**

```cpp
awaitable<bool> handle_auth_response(bytes payload) {
  for (;;) {
    if (is_err_packet(payload)) {
      throw runtime_error("auth failed");
    }

    if (is_ok_packet(payload)) {
      apply_ok(parse_ok_packet(payload));
      co_return true;
    }

    if (is_auth_switch_request(payload)) {
      // 服务端要求切换认证插件
      auto plugin_name = read_plugin_name(payload);
      auto plugin_data = read_plugin_data(payload);

      // 更新 scramble 和插件名
      update_auth_state(plugin_name, plugin_data);

      // 重新发送认证响应
      co_await write_packet(build_auth_response(plugin_name));
      payload = co_await read_packet();
      continue;
    }

    if (is_auth_more_data(payload)) {
      // caching_sha2_password 的额外流程
      auto data = read_auth_data(payload);

      if (fast_auth_success(data)) {
        payload = co_await read_packet();
        continue;
      }

      // 需要 RSA 加密或请求公钥
      if (!is_secure_channel()) {
        co_await request_public_key_or_encrypt();
        payload = co_await read_packet();
        continue;
      }
    }

    throw runtime_error("unexpected auth packet");
  }
}
```

### 包协议

MySQL 协议以定长 4 字节头（3 字节长度 + 1 字节序列号）分帧：

```
┌────────────────────────────────────────┐
│  Packet Header (4 bytes)               │
├────────────────────────────────────────┤
│  Payload Length (3 bytes, little-endian)│
│  Sequence ID (1 byte)                  │
├────────────────────────────────────────┤
│  Payload (variable length)             │
│  ...                                   │
└────────────────────────────────────────┘
```

#### 读取包

```cpp
awaitable<bytes> read_packet() {
  bytes payload;

  for (;;) {
    // 读取 4 字节头
    std::array<byte, 4> header_buf;
    co_await asio::async_read(*socket_, asio::buffer(header_buf),
                              asio::use_awaitable);

    packet_header header;
    header.payload_size = read_le24(header_buf.data());
    header.sequence_id = header_buf[3];
    sequence_id_ = static_cast<byte>(header.sequence_id + 1);

    // 读取 payload
    bytes chunk(header.payload_size);
    if (header.payload_size > 0) {
      co_await asio::async_read(*socket_, asio::buffer(chunk),
                                asio::use_awaitable);
    }

    payload.insert(payload.end(), chunk.begin(), chunk.end());

    // 如果 payload 大小等于最大块大小，说明还有后续包
    if (header.payload_size != max_packet_chunk) {
      break;
    }
  }

  co_return payload;
}
```

**关键设计**：
- 超过 16MB 的数据自动分多包发送
- 序列号自动递增，用于检测包乱序
- 支持多包拼接

#### 写入包

```cpp
awaitable<void> write_packet(bytes payload) {
  std::size_t offset = 0;

  do {
    auto remaining = payload.size() - offset;
    auto chunk_size = std::min<std::size_t>(remaining, max_packet_chunk);

    // 构造帧：4 字节头 + payload 块
    bytes frame;
    frame.reserve(chunk_size + 4);
    append_le24(frame, static_cast<std::uint32_t>(chunk_size));
    frame.push_back(sequence_id_++);
    frame.insert(frame.end(),
                 payload.begin() + offset,
                 payload.begin() + offset + chunk_size);

    co_await asio::async_write(*socket_, asio::buffer(frame),
                               asio::use_awaitable);

    offset += chunk_size;

    // 如果恰好在边界结束，发送空包作为终止符
    if (chunk_size == max_packet_chunk && offset == payload.size()) {
      bytes empty_frame = {0, 0, 0, sequence_id_++};
      co_await asio::async_write(*socket_, asio::buffer(empty_frame),
                                 asio::use_awaitable);
    }
  } while (offset < payload.size());
}
```

### 查询执行

文本协议查询流程：

```
客户端                                    服务端
  │                                         │
  │  1. write COM_QUERY + SQL               │
  │────────────────────────────────────────▶│
  │                                         │
  │  2. read response                       │
  │◀────────────────────────────────────────│
  │  - OK packet (无结果集)                  │
  │  - Error packet (查询失败)               │
  │  - Column count (有结果集)               │
  │                                         │
  │  3. read column definitions (N 个)      │
  │◀────────────────────────────────────────│
  │                                         │
  │  4. read rows (M 个)                    │
  │◀────────────────────────────────────────│
  │                                         │
  │  5. read EOF/OK packet                  │
  │◀────────────────────────────────────────│
```

#### 实现细节

```cpp
awaitable<query_result> query_text(const std::string& sql) {
  // 1. 发送 COM_QUERY 命令
  co_return co_await command_simple(0x03, sql);
}

awaitable<query_result> command_simple(byte command,
                                       const std::string& payload) {
  bytes request;
  request.push_back(command);
  request.insert(request.end(), payload.begin(), payload.end());

  sequence_id_ = 0;
  co_await write_packet(std::move(request));
  co_return co_await read_query_response();
}

awaitable<query_result> read_query_response() {
  auto payload = co_await read_packet();

  // 2. 检查响应类型
  if (is_err_packet(payload)) {
    auto err = parse_error_packet(payload);
    throw runtime_error("query failed [" + to_string(err.code) + "] " +
                       err.message);
  }

  query_result result;

  if (is_ok_packet(payload)) {
    // 无结果集（INSERT/UPDATE/DELETE）
    result.ok = parse_ok_packet(payload);
    apply_ok(result.ok);
    co_return result;
  }

  // 3. 有结果集，读取列定义
  packet_reader rd(payload);
  auto column_count = rd.read_lenenc_int();
  result.has_resultset = true;
  result.columns.reserve(static_cast<std::size_t>(column_count));

  for (std::uint64_t i = 0; i < column_count; ++i) {
    result.columns.push_back(
        parse_column_definition(co_await read_packet()));
  }

  // 4. 读取行数据
  for (;;) {
    auto row_payload = co_await read_packet();

    if (is_err_packet(row_payload)) {
      auto err = parse_error_packet(row_payload);
      throw runtime_error("row fetch failed");
    }

    if (looks_like_eof_packet(row_payload)) {
      // 5. EOF/OK 包，结束
      apply_ok(parse_ok_packet(row_payload));
      break;
    }

    result.rows.push_back(
        parse_text_row(row_payload, result.columns.size()));
  }

  last_affect_rows_ = static_cast<int>(result.rows.size());
  co_return result;
}
```

### SQL 参数替换

`mysql_async` 不使用服务端 Prepared Statement，而是在客户端做字符串替换。

#### 占位符查找

```cpp
std::size_t find_next_placeholder(std::string_view sql,
                                  std::size_t start_pos,
                                  bool no_backslash_escapes) {
  bool in_string = false;
  bool in_comment = false;
  char string_delimiter = '\0';

  for (std::size_t i = start_pos; i < sql.size(); ++i) {
    char ch = sql[i];

    // 处理注释
    if (!in_string && i + 1 < sql.size()) {
      if (sql[i] == '-' && sql[i + 1] == '-') {
        in_comment = true;
        ++i;
        continue;
      }
    }

    if (in_comment) {
      if (ch == '\n') in_comment = false;
      continue;
    }

    // 处理字符串字面量
    if (!in_string && (ch == '\'' || ch == '"')) {
      in_string = true;
      string_delimiter = ch;
      continue;
    }

    if (in_string) {
      if (ch == string_delimiter) {
        // 检查是否是转义的引号
        if (i + 1 < sql.size() && sql[i + 1] == string_delimiter) {
          ++i;  // 跳过转义的引号
          continue;
        }
        in_string = false;
      } else if (!no_backslash_escapes && ch == '\\' && i + 1 < sql.size()) {
        ++i;  // 跳过转义字符
      }
      continue;
    }

    // 找到占位符
    if (ch == '?') {
      return i;
    }
  }

  return std::string_view::npos;
}
```

**关键设计**：
- 跳过注释中的 `?`
- 跳过字符串字面量中的 `?`（如 `'a?b'`）
- 处理转义引号（`''` 或 `\'`）
- 尊重 `NO_BACKSLASH_ESCAPES` 模式

#### 参数转义

```cpp
std::string escape_mysql_string(std::string_view input,
                                bool no_backslash_escapes) {
  std::string result;
  result.reserve(input.size() * 2 + 2);
  result.push_back('\'');

  for (unsigned char ch : input) {
    if (no_backslash_escapes) {
      // NO_BACKSLASH_ESCAPES 模式：只转义单引号
      if (ch == '\'') {
        result.push_back('\'');
        result.push_back('\'');
      } else {
        result.push_back(static_cast<char>(ch));
      }
      continue;
    }

    // 标准模式：转义特殊字符
    switch (ch) {
      case 0:    result += "\\0"; break;
      case '\n': result += "\\n"; break;
      case '\r': result += "\\r"; break;
      case '\\': result += "\\\\"; break;
      case '\'': result += "\\'"; break;
      case '"':  result += "\\\""; break;
      case '\x1a': result += "\\Z"; break;
      default:   result.push_back(static_cast<char>(ch)); break;
    }
  }

  result.push_back('\'');
  return result;
}
```

#### 结构体字段序列化

```cpp
template <typename Tag, typename T, typename... Args>
std::string format_struct_sql(const std::string& sql, const T& value,
                              OptType type, Args&&... args) const {
  std::vector<std::string> values;
  values.reserve(ylt::reflection::members_count_v<T> * 2 + sizeof...(Args));

  // 1. 序列化结构体字段
  ylt::reflection::for_each(
      value, [&](auto& field, auto name, auto /*index*/) {
        if (type == OptType::insert && is_auto_key<T>(name)) {
          return;  // INSERT 时跳过自增主键
        }
        values.push_back(to_query_arg_impl(field, !backslash_escapes_));
      });

  // 2. 添加额外参数（如 WHERE 条件）
  if constexpr (sizeof...(Args) > 0) {
    (values.push_back(to_query_arg_impl(
         std::forward<Args>(args), !backslash_escapes_)),
     ...);
  }

  // 3. 替换占位符
  std::string formatted = sql;
  std::size_t search_pos = 0;
  for (auto& item : values) {
    replace_next_placeholder(formatted, search_pos, std::move(item),
                            !backslash_escapes_);
  }

  // 4. 验证所有占位符都已替换
  if (find_next_placeholder(formatted, search_pos, !backslash_escapes_) !=
      std::string_view::npos) {
    throw runtime_error("placeholder count mismatch");
  }

  return formatted;
}
```

### 私有字段

```cpp
class mysql_async {
 private:
  // 执行器和 I/O 对象
  executor_type executor_;
  asio::ip::tcp::resolver resolver_;
  asio::steady_timer timer_;
  std::optional<asio::ip::tcp::socket> socket_;

  // 连接标识
  std::string host_;
  std::string user_;
  std::string password_;
  std::string database_;
  int port_;
  int timeout_seconds_;

  // 连接状态
  bool connected_ = false;
  std::uint32_t server_capabilities_;
  std::uint32_t negotiated_capabilities_;
  std::uint32_t max_packet_size_ = 0x00ffffff;
  byte collation_id_ = 33;  // utf8_general_ci
  byte sequence_id_ = 0;
  std::uint16_t status_flags_;
  bool backslash_escapes_ = true;
  std::string auth_plugin_name_;
  std::array<byte, 20> scramble_;

  // 执行状态
  std::uint64_t last_insert_id_ = 0;
  int last_affect_rows_ = 0;
  std::string last_error_;
  bool has_error_ = false;
  bool transaction_ = false;
};
```

---


## 第二层：协程框架桥接

### 问题：两套协程框架不兼容

ormpp 使用 ASIO 协程实现异步数据库接口，而业务代码使用 async_simple 协程框架。两者不能直接互相 `co_await`：

| | asio::awaitable | async_simple::Lazy |
|---|---|---|
| 调度器 | Asio executor | async_simple executor |
| 挂起机制 | Asio 内部 | async_simple 内部 |
| `co_await` | 只能在 Asio 协程中用 | 只能在 Lazy 协程中用 |

需要一个适配器做桥接。

### 设计约束

ormpp 提供两个版本的适配器：

#### **轻量版 (AsioAwaitableAdapter)**

**设计前提**：
1. **单线程 executor**：同一个 `io_context` 只有一个线程 `run()`
2. **共享执行上下文**：Asio executor 和 async_simple executor 指向同一底层 `io_context`
3. **结构化并发**：父 `Lazy` 协程在等待期间不会被外部取消或销毁

在这些约束下，适配器可以省去 `shared_ptr`、mutex 和原子变量，结果直接存储在 awaiter 对象里。

#### **安全版 (SafeAsioAwaitableAdapter)**

**适用场景**：
1. **多线程 executor**：多个线程同时 `run()` 同一个 `io_context`
2. **awaiter 可能提前销毁**：父协程可能在等待中被取消或销毁
3. **需要取消操作**：支持通过 cancellation handle 请求取消

### 轻量版实现

#### 类结构

```cpp
template <typename T>
class AsioAwaitableAdapter {
 private:
  using stored_type = std::conditional_t<std::is_void_v<T>, std::monostate, T>;

  asio::awaitable<T, asio::any_io_executor> awaitable_;
  asio::any_io_executor asio_executor_;
  std::optional<stored_type> result_;
  std::exception_ptr exception_;
  std::coroutine_handle<> resume_handle_;

  void post_resume() noexcept;
  asio::awaitable<void, asio::any_io_executor> run_asio();
  void start_asio() noexcept;

 public:
  AsioAwaitableAdapter(asio::awaitable<T> awaitable,
                       asio::any_io_executor executor);

  bool await_ready() const noexcept { return false; }
  bool await_suspend(std::coroutine_handle<>) noexcept;
  T await_resume();
  auto coAwait(async_simple::Executor*) noexcept;
};
```

#### 三个关键方法

**1. await_ready()**

```cpp
bool await_ready() const noexcept {
  return false;  // 固定返回 false
}
```

Asio 协程只在 `await_suspend()` 之后启动，不存在"构造后、挂起前已经完成"的情况。

**2. await_suspend()**

```cpp
bool await_suspend(std::coroutine_handle<> handle) noexcept {
  resume_handle_ = handle;  // 保存父协程

  try {
    // 必须用 post，不能直接调用 start_asio()
    asio::post(asio_executor_, [this]() {
      start_asio();
    });
  } catch (...) {
    exception_ = std::current_exception();
    return false;  // 不挂起，立即恢复
  }

  return true;  // 挂起父协程
}
```

**为什么用 post 而不是直接调用？**

`co_spawn` 会立即开始执行协程直到第一个真正挂起点。对于同步完成的 awaitable（如 `co_return value`），整个执行链会在 `await_suspend` 返回前完成，导致 `post_resume()` 在 `await_suspend` 仍在栈上时恢复父协程，产生生命周期冲突。

测试验证的顺序：
```
["handler-enter", "task-before-await", "handler-exit"]  // await_suspend 返回
["child-start"]                                          // 然后 Asio 子协程启动
["task-after-await"]                                     // 最后父协程恢复
```

**3. await_resume()**

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

#### 启动和恢复流程

```cpp
void start_asio() noexcept {
  try {
    asio::co_spawn(
        asio_executor_,
        run_asio(),
        [this](std::exception_ptr exception) {
          if (exception && !exception_) {
            exception_ = exception;
          }
          post_resume();
        });
  } catch (...) {
    exception_ = std::current_exception();
    post_resume();
  }
}

asio::awaitable<void> run_asio() {
  try {
    if constexpr (std::is_void_v<T>) {
      co_await std::move(awaitable_);
      result_.emplace();
    } else {
      result_.emplace(co_await std::move(awaitable_));
    }
  } catch (...) {
    exception_ = std::current_exception();
  }
}

void post_resume() noexcept {
  auto handle = resume_handle_;
  try {
    // 必须用 post，不能直接 handle.resume()
    asio::post(asio_executor_, [handle]() mutable {
      handle.resume();
    });
  } catch (...) {
    if (!exception_) {
      exception_ = std::current_exception();
    }
    handle.resume();  // fallback
  }
}
```

**为什么 post_resume 也用 post？**

`run_asio()` 协程完成后调用 `post_resume()`。此时 `run_asio()` 还在栈上。若直接 `handle.resume()`，父协程立即恢复并销毁 awaiter，而 `run_asio()` 还在用 `this`，形成悬空指针。

#### coAwait 接口

```cpp
auto coAwait(async_simple::Executor*) noexcept {
  return std::move(*this);
}
```

async_simple 通过 `coAwait()` 识别自定义 awaiter。`executor` 参数在此不需要，因为恢复直接投递到已保存的 `asio_executor_`。

### 安全版实现

#### 类结构

```cpp
template <typename T>
class SafeAsioAwaitableAdapter {
 private:
  using executor_type = asio::any_io_executor;
  using strand_type = asio::strand<executor_type>;
  using stored_type = std::conditional_t<std::is_void_v<T>, std::monostate, T>;

  struct SharedState {
    asio::awaitable<T, executor_type> awaitable;
    executor_type executor;
    strand_type strand;
    std::optional<stored_type> result;
    std::exception_ptr exception;
    std::coroutine_handle<> resume_handle;
    bool finished = false;
    std::atomic_bool await_started{false};
    std::atomic_bool abandoned{false};
    std::atomic_bool resume_observed{false};
    std::atomic<async_simple::Executor*> async_executor{nullptr};

#if ORMPP_ASIO_ASYNC_SIMPLE_ADAPTER_HAS_CANCELLATION
    asio::cancellation_signal cancellation;
    asio::cancellation_type pending_cancellation;
#endif
  };

  std::shared_ptr<SharedState> state_;

 public:
  class cancellation_handle {
   public:
    bool valid() const noexcept;
    bool cancel(asio::cancellation_type = ...) const noexcept;
  };

  SafeAsioAwaitableAdapter(asio::awaitable<T>, executor_type);
  ~SafeAsioAwaitableAdapter();  // 调用 abandon()

  bool await_ready() const noexcept;
  bool await_suspend(std::coroutine_handle<>) noexcept;
  T await_resume();

  cancellation_handle get_cancellation_handle() const noexcept;
  bool cancel(...) const noexcept;
  auto coAwait(async_simple::Executor*) noexcept;
};
```

#### 关键差异

**1. SharedState 独立生命周期**

```cpp
std::shared_ptr<SharedState> state_;
```

- Asio 侧协程持有 `shared_ptr`，即使 awaiter 被销毁，状态仍然存在
- 防止悬空指针访问

**2. strand 串行化**

```cpp
strand_type strand;
```

所有非 atomic 状态的修改都通过 strand 串行化：
- `start_asio()` 在 strand 上
- `complete_asio()` 在 strand 上
- `resume_parent()` 在 strand 上
- `request_cancel()` 在 strand 上

**3. atomic 标志**

```cpp
std::atomic_bool abandoned{false};
std::atomic_bool resume_observed{false};
std::atomic<async_simple::Executor*> async_executor{nullptr};
```

- `abandoned`：awaiter 被销毁，不应再恢复父协程
- `resume_observed`：`await_resume()` 已被调用，不应再恢复
- `async_executor`：可选的 async_simple executor，用于调度恢复

**4. 取消支持**

```cpp
#if ORMPP_ASIO_ASYNC_SIMPLE_ADAPTER_HAS_CANCELLATION
asio::cancellation_signal cancellation;
asio::cancellation_type pending_cancellation;
#endif
```

- 条件编译，仅在 ASIO 支持取消时启用
- `pending_cancellation` 记录 `co_spawn` 前的取消请求

#### 关键流程

**启动流程**

```cpp
bool await_suspend(std::coroutine_handle<> handle) noexcept {
  if (!state_) return false;

  state_->resume_handle = handle;
  state_->await_started.store(true, std::memory_order_release);

  try {
    // 投递到 strand
    asio::post(state_->strand, [state = state_]() mutable {
      start_asio(std::move(state));
    });
  } catch (...) {
    state_->exception = std::current_exception();
    state_->finished = true;
    return false;
  }

  return true;
}

static void start_asio(std::shared_ptr<SharedState> state) noexcept {
  if (state->abandoned.load(std::memory_order_acquire)) {
    return;  // 已被放弃
  }

  try {
#if ORMPP_ASIO_ASYNC_SIMPLE_ADAPTER_HAS_CANCELLATION
    auto cancel_slot = state->cancellation.slot();
    auto completion_token = asio::bind_cancellation_slot(
        cancel_slot,
        asio::bind_executor(state->strand, [state](...) {
          complete_asio(std::move(state), exception);
        }));

    asio::co_spawn(state->strand, run_asio(state), completion_token);

    // 如果在 co_spawn 前已有取消请求，立即 emit
    if (state->pending_cancellation != asio::cancellation_type::none) {
      state->cancellation.emit(state->pending_cancellation);
    }
#else
    asio::co_spawn(state->strand, run_asio(state), ...);
#endif
  } catch (...) {
    state->exception = std::current_exception();
    state->finished = true;
    post_resume(std::move(state));
  }
}
```

**恢复流程**

```cpp
static bool should_resume(const std::shared_ptr<SharedState>& state) noexcept {
  return state &&
         !state->abandoned.load(std::memory_order_acquire) &&
         !state->resume_observed.load(std::memory_order_acquire) &&
         state->resume_handle;
}

static void resume_parent(std::shared_ptr<SharedState> state) noexcept {
  if (!should_resume(state)) {
    return;  // 不应恢复
  }

  auto handle = state->resume_handle;
  auto* async_executor = state->async_executor.load(std::memory_order_acquire);

  if (async_executor) {
    try {
      // 尝试在 async_simple executor 上调度
      if (async_executor->schedule([state, handle]() mutable {
            if (should_resume(state)) {
              handle.resume();
            }
          })) {
        return;
      }
    } catch (...) {
      if (!state->exception) {
        state->exception = std::current_exception();
      }
    }
  }

  // fallback：直接恢复
  handle.resume();
}
```

**取消流程**

```cpp
static bool request_cancel(const std::shared_ptr<SharedState>& state,
                          asio::cancellation_type type) noexcept {
  if (!state) return false;
  if (state->resume_observed.load(std::memory_order_acquire)) {
    return false;
  }

  try {
    asio::post(state->strand, [state, type]() mutable {
      if (state->finished ||
          state->resume_observed.load(std::memory_order_acquire)) {
        return;  // 已完成，取消无效
      }

      state->pending_cancellation |= type;
      state->cancellation.emit(type);
    });
    return true;
  } catch (...) {
    return false;
  }
}
```

**abandon 流程**

```cpp
static void abandon(std::shared_ptr<SharedState> state) noexcept {
  if (!state || state->resume_observed.load(std::memory_order_acquire)) {
    return;
  }

  state->abandoned.store(true, std::memory_order_release);
  if (!state->await_started.load(std::memory_order_acquire)) {
    return;  // 构造后尚未 co_await，销毁时无需额外投递取消任务
  }

#if ORMPP_ASIO_ASYNC_SIMPLE_ADAPTER_HAS_CANCELLATION
  request_cancel(state, asio::cancellation_type::all);
#endif
}

~SafeAsioAwaitableAdapter() {
  abandon(std::move(state_));
}
```

### 性能对比

#### 内存开销

| 版本 | 栈 | 堆 | 总计 |
|---|---|---|---|
| 轻量版 | ~56-64 bytes | 0 | ~56-64 bytes |
| 安全版 | ~8 bytes | ~136-152 bytes | ~144-160 bytes |
| **增加** | | | **~2.5x** |

#### CPU 开销

| 操作 | 轻量版 | 安全版 | 增加 |
|---|---|---|---|
| 构造 | 移动 awaitable | + `make_shared` | +1 堆分配 |
| post 调度 | 2x | 4x | +2x |
| strand 调度 | 0x | 4x | +4x |
| atomic 操作 | 0x | 3x load | +3x |
| **纯开销** | **~2 μs** | **~6 μs** | **~3x** |

#### 相对于数据库 I/O 的开销

| 场景 | 查询时间 | 轻量版开销 | 安全版开销 | 额外开销 | 影响 |
|---|---|---|---|---|---|
| 本地查询 | 100 μs | 2 μs (2%) | 6 μs (6%) | 4 μs | ~4% |
| 远程查询 | 1 ms | 2 μs (0.2%) | 6 μs (0.6%) | 4 μs | ~0.4% |
| 远程查询 | 10 ms | 2 μs (0.02%) | 6 μs (0.06%) | 4 μs | ~0.04% |

**结论**：对于绝大多数实际应用场景，安全版的额外开销相对于数据库 I/O 可以忽略。

### 选择指南

| 场景 | 推荐版本 |
|---|---|
| 单线程 `io_context::run()` | 轻量版 |
| 多线程 `io_context::run()` | 安全版 |
| 父协程生命周期明确 | 轻量版 |
| awaiter 可能提前销毁 | 安全版 |
| 不需要取消操作 | 轻量版 |
| 需要取消操作 | 安全版 |
| 追求极致性能（高频短查询） | 轻量版 |
| 查询时间 > 100 μs | 安全版（开销可忽略） |

---


## 第三层：链式查询 API

### 统一同步/异步的 trait

```cpp
template <typename DB>
struct db_execution_traits {
  static constexpr bool is_async = false;
  template <typename T>
  using awaitable_type = T;
};

// mysql_async 特化
template <>
struct db_execution_traits<mysql_async> {
  static constexpr bool is_async = true;
  template <typename T>
  using awaitable_type = asio::awaitable<T, asio::any_io_executor>;
};

template <typename DB>
inline constexpr bool is_async_db_v =
    db_execution_traits<std::remove_pointer_t<DB>>::is_async;

template <typename DB, typename T>
using db_awaitable_t =
    typename db_execution_traits<std::remove_pointer_t<DB>>::template
        awaitable_type<T>;
```

链式查询的 `collect()`/`execute()` 通过 `if constexpr (is_async_db_v<DB>)` 选择同步或异步路径。

### 链式构造阶段

链式 API 通过一系列阶段结构体实现类型安全的流式调用：

```
select(...) → stage_where → stage_group_by → stage_having
→ stage_order → stage_limit → stage_offset → collect()/scalar()
```

每个阶段持有一个共享的 `context` 对象（`shared_ptr`），累积 SQL 子句：

```cpp
template <typename T, typename DB, typename R = void>
class query_builder {
  struct context {
    DB db_;
    std::string sql_;
    std::string select_clause_;
    std::string from_clause_;
    std::string join_clause_;
    std::string where_clause_;
    std::string group_by_clause_;
    std::string having_clause_;
    std::string order_by_clause_;
    std::string desc_clause_;
    std::string limit_clause_;
    std::string offset_clause_;
    std::string count_clause_;
    std::string sum_clause_;
    std::string avg_clause_;
    std::string min_clause_;
    std::string max_clause_;

    std::string build_sql() const {
      std::string sql = sql_;

      if (!select_clause_.empty()) {
        sql.append("select ").append(select_clause_).append(from_clause_);
        if (!where_clause_.empty()) {
          sql.insert(0, " where ");
        }
      }

      sql.append(join_clause_)
         .append(where_clause_)
         .append(group_by_clause_)
         .append(having_clause_)
         .append(order_by_clause_)
         .append(desc_clause_)
         .append(limit_clause_)
         .append(offset_clause_);

      return sql;
    }
  };
};
```

### 占位符机制

`token_t` 是编译期标记类型，表示"运行时再传值"：

```cpp
struct token_t {};
inline constexpr auto token = token_t{};
```

使用示例：

```cpp
co_await db.select(all)
    .from<person>()
    .where(col(&person::name).param())   // → WHERE name = ?
    .limit(token)                         // → LIMIT ?
    .offset(token)                        // → OFFSET ?
    .collect("Alice", 10, 0);            // Alice → name, 10 → LIMIT, 0 → OFFSET
```

**实现细节**：

```cpp
// limit/offset 阶段
struct stage_limit {
  std::shared_ptr<context> ctx;

  stage_limit limit(uint64_t n) {
    ctx->limit_clause_ = " LIMIT " + std::to_string(n);
    return stage_limit{ctx};
  }

  stage_limit limit(token_t) {
    ctx->limit_clause_ = " LIMIT ?  ";  // 占位符
    return stage_limit{ctx};
  }

  stage_offset offset(uint64_t row) {
    ctx->offset_clause_ = " OFFSET " + std::to_string(row);
    return stage_offset{ctx};
  }

  stage_offset offset(token_t) {
    ctx->offset_clause_ = " OFFSET ?  ";  // 占位符
    return stage_offset{ctx};
  }
};
```

固定值可以直接写在链式调用里，省去 `collect()` 传参：

```cpp
.limit(10)    // → LIMIT 10（直接嵌入 SQL，不用 token）
.offset(0)    // → OFFSET 0
```

### 异步 collect 的实现

异步路径将参数打包进 `tuple`，在协程中展开：

```cpp
template <typename To = void, typename... Args>
requires(is_async_db_v<DB>)
auto collect(Args... args) {
  return collect_async<To>(
      this->shared_from_this(),
      std::make_tuple(std::move(args)...),
      std::index_sequence_for<Args...>{});
}

template <typename To, typename Tuple, std::size_t... I>
static db_awaitable_t<DB, collect_result_t<To>> collect_async(
    std::shared_ptr<context> ctx, Tuple params, std::index_sequence<I...>) {
  auto sql = ctx->build_sql();

  if constexpr (!ylt::reflection::is_ylt_refl_v<R> && !std::is_void_v<R> &&
                !iguana::tuple_v<R>) {
    auto result = co_await ctx->template query_async_with_params<
        std::tuple<R>>(sql, params, std::index_sequence<I...>{});
    co_return extract_query_result<To>(std::move(result));
  }
  else if constexpr (std::is_void_v<R>) {
    auto result = co_await ctx->template query_async_with_params<T>(
        sql, params, std::index_sequence<I...>{});
    co_return extract_query_result<To>(std::move(result));
  }
  // ... 其他分支
}

template <typename Out, typename Tuple, std::size_t... I>
db_awaitable_t<DB, std::vector<Out>> query_async_with_params(
    const std::string& sql, Tuple& params, std::index_sequence<I...>) {
  co_return co_await db_->template query_s<Out>(
      sql, std::decay_t<decltype(std::get<I>(params))>(std::get<I>(params))...);
}
```

**关键设计**：
- `context` 用 `shared_ptr` 管理，确保在异步等待期间链式对象不被提前析构
- 参数通过 `index_sequence` 展开，传递给底层的 `query_s`

### 可用的链式操作

#### **SELECT 查询**

```cpp
co_await db.select(all)
    .from<person>()
    .where(col(&person::age) > 18 && col(&person::name).param())
    .order_by(col(&person::id).desc())
    .group_by(col(&person::age))
    .having(count() > 1)
    .limit(token).offset(token)
    .collect("Alice", 20, 0);
```

**支持的聚合函数**：
- `count()` / `count(col(...))`
- `count_distinct(col(...))`
- `sum(col(...))`
- `avg(col(...))`
- `min(col(...))`
- `max(col(...))`

**支持的条件表达式**：
```cpp
// 比较运算
col(&person::age) > 18
col(&person::name) == "Alice"

// IN 查询
col(&person::id).in(1, 2, 3)

// BETWEEN 查询
col(&person::name).between("A", "Z")

// 参数化查询
col(&person::name).param()  // 然后在 collect() 中传参
```

#### **UPDATE 操作**

```cpp
co_await db.update<person>()
    .set(col(&person::age), 30)
    .where(col(&person::id) == 1)
    .execute();
```

#### **DELETE 操作**

```cpp
co_await db.remove<person>()
    .where(col(&person::id) == 1)
    .execute();
```

#### **scalar 查询**（返回单个值）

```cpp
auto age = co_await db.select(col(&person::age))
               .from<person>()
               .where(col(&person::name).param())
               .scalar(std::string("Alice"));
```

#### **subset 查询**（结果映射到部分字段结构体）

```cpp
struct person_subset {
  std::string name;
  int age;
};
YLT_REFL(person_subset, name, age)

auto rows = co_await db
    .select(col(&person::name), col(&person::age))
    .from<person>()
    .collect<person_subset>();
```

### JOIN 支持

```cpp
co_await db.select(all)
    .from<person>()
    .inner_join(col(&person::dept_id), col(&department::id))
    .where(col(&department::name) == "Engineering")
    .collect();
```

---

## 第四层：连接池

`async_connection_pool<DB>` 在 `mysql_async` 之上提供连接复用。

### 类结构

```cpp
struct pool_options {
  bool enable_dynamic_expansion = false;  // 允许临时连接
  size_t max_dynamic_connections = 10;    // 最大临时连接数
  bool log_pool_exhaustion = true;        // 记录池耗尽日志
};

template <typename DB>
requires is_async_db_v<DB>
class async_connection_pool {
 public:
  using executor_type = asio::any_io_executor;
  template <typename T>
  using awaitable = asio::awaitable<T, executor_type>;

  explicit async_connection_pool(executor_type executor,
                                 pool_options options = {});
  ~async_connection_pool();

  // 初始化连接池
  awaitable<bool> init(size_t pool_size, host, user, passwd, db,
                       timeout, port);

  // 获取连接（带超时）
  awaitable<std::shared_ptr<DB>> get(
      std::chrono::seconds timeout = std::chrono::seconds(10));

  // 统计信息
  awaitable<std::tuple<size_t, size_t, size_t, size_t>> get_stats();
  awaitable<std::string> get_status_string();

  // 关闭所有连接
  awaitable<void> close_all(bool wait_for_return = false,
                            std::chrono::seconds max_wait = 30s);

 private:
  executor_type executor_;
  asio::strand<executor_type> strand_;
  pool_options options_;

  bool initialized_ = false;
  bool closing_ = false;
  size_t pool_size_ = 0;
  size_t in_use_count_ = 0;
  size_t dynamic_connection_count_ = 0;

  std::string host_, user_, passwd_, database_;
  std::optional<int> timeout_, port_;

  std::deque<std::unique_ptr<DB>> available_connections_;
};
```

### 初始化

```cpp
awaitable<bool> init(size_t pool_size, ...) {
  if (initialized_) co_return true;
  if (closing_) co_return false;

  pool_size_ = pool_size;
  host_ = host;
  user_ = user;
  // ... 保存连接参数

  // 预创建连接
  for (size_t i = 0; i < pool_size_; ++i) {
    auto conn = std::make_unique<DB>(executor_);

    bool connected = co_await conn->connect(host_, user_, passwd_,
                                            database_, timeout_, port_);
    if (!connected) {
      co_await disconnect_available_connections();
      co_return false;
    }

    available_connections_.push_back(std::move(conn));
  }

  initialized_ = true;
  co_return true;
}
```

### 获取连接

```cpp
awaitable<std::shared_ptr<DB>> get(std::chrono::seconds timeout) {
  auto deadline = std::chrono::steady_clock::now() + timeout;
  size_t wait_count = 0;

  while (std::chrono::steady_clock::now() < deadline) {
    // 切换到 strand（线程安全）
    co_await asio::post(strand_, asio::use_awaitable);

    if (!initialized_ || closing_) {
      co_return nullptr;
    }

    if (!available_connections_.empty()) {
      // 取出一个连接
      auto connection = std::move(available_connections_.front());
      available_connections_.pop_front();
      in_use_count_++;

      // 健康检查
      bool alive = co_await connection->ping();
      if (!alive) {
        // 重连
        alive = co_await connection->connect(host_, user_, passwd_,
                                             database_, timeout_, port_);
        if (!alive) {
          co_await asio::post(strand_, asio::use_awaitable);
          if (in_use_count_ > 0) --in_use_count_;
          co_await connection->disconnect();
          continue;
        }
      }

      co_await asio::post(strand_, asio::use_awaitable);
      if (!initialized_ || closing_) {
        if (in_use_count_ > 0) --in_use_count_;
        co_await connection->disconnect();
        continue;
      }

      // 返回带自定义 deleter 的 shared_ptr
      co_return make_connection_handle(std::move(connection), false);
    }

    // 池耗尽
    wait_count++;

    // 尝试动态扩容
    if (options_.enable_dynamic_expansion &&
        dynamic_connection_count_ < options_.max_dynamic_connections) {
      auto temp_conn = co_await create_dynamic_connection();
      if (temp_conn) {
        co_return temp_conn;
      }
    }

    // 记录日志
    if (wait_count == 1 && options_.log_pool_exhaustion) {
      log_pool_exhausted();
    }

    // 指数退避等待
    auto wait_time = std::min(
        std::chrono::milliseconds(50 * (1 << std::min(wait_count - 1, size_t(3)))),
        std::chrono::milliseconds(500));
    auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
        deadline - std::chrono::steady_clock::now());
    if (remaining <= std::chrono::milliseconds::zero()) {
      break;
    }
    wait_time = std::min(wait_time, remaining);

    asio::steady_timer retry_timer(executor_);
    retry_timer.expires_after(wait_time);
    co_await retry_timer.async_wait(asio::use_awaitable);
  }

  // 超时
  if (options_.log_pool_exhaustion) {
    log_connection_timeout(timeout);
  }

  co_return nullptr;
}
```

### 自动归还机制

```cpp
std::shared_ptr<DB> make_connection_handle(std::unique_ptr<DB> connection,
                                           bool dynamic) {
  auto raw = connection.release();
  auto weak_pool = this->weak_from_this();

  try {
    return std::shared_ptr<DB>(
        raw, [weak_pool, dynamic](DB* db) mutable {
          std::unique_ptr<DB> connection(db);
          try {
            if (auto pool = weak_pool.lock()) {
              auto strand = pool->strand_;
              asio::post(strand,
                         [pool = std::move(pool),
                          connection = std::move(connection),
                          dynamic]() mutable {
                           pool->return_connection(std::move(connection),
                                                   dynamic);
                         });
            }
          } catch (...) {
            // 投递失败时，connection 在本地析构并关闭 socket。
          }
        });
  } catch (...) {
    delete raw;
    throw;
  }
}

void return_connection(std::unique_ptr<DB> connection, bool dynamic) {
  if (dynamic) {
    if (dynamic_connection_count_ > 0) {
      --dynamic_connection_count_;
    }
    if (in_use_count_ > 0) {
      --in_use_count_;
    }
    return;  // 动态连接用完即销毁，不回到固定池
  }

  if (in_use_count_ > 0) {
    --in_use_count_;
  }

  if (initialized_ && !closing_) {
    available_connections_.push_back(std::move(connection));
  }
}
```

这里使用 `weak_from_this()` 而不是在 deleter 中捕获裸 `this`。如果连接句柄比连接池活得更久，
`weak_pool.lock()` 会失败，连接对象在 deleter 本地析构，避免访问已销毁的 pool。
非动态连接会异步投递回 strand 后放回 `available_connections_`；动态连接只更新计数并销毁，
不会进入固定连接池。

### 关键设计

1. **strand 保护**：所有对 `available_connections_` 的操作都先 `co_await asio::post(strand_, ...)` 切换到 strand，避免并发访问

2. **自动归还**：`get()` 返回带自定义 deleter 的 `shared_ptr`，析构时将连接投递回 strand；用户不需要手动归还连接

3. **健康检查**：取出连接前 `ping()`，失败则重连；重连失败则丢弃并重试下一个

4. **动态扩容**：`pool_options::enable_dynamic_expansion` 开启后，池满时可临时创建额外连接，用完后断开而不归还

5. **等待退避**：池满时按 50ms → 100ms → 200ms → 500ms 指数退避，直到超时返回 `nullptr`

### 使用示例

```cpp
auto pool = std::make_shared<async_connection_pool<mysql_async>>(executor);
co_await pool->init(8, "127.0.0.1", "root", "", "mydb");

{
  auto conn = co_await pool->get(std::chrono::seconds(5));
  if (!conn) {
    // 超时，池耗尽
    co_return;
  }

  // 使用连接
  co_await conn->insert(person{0, "Alice", 25});
  auto rows = co_await conn->query_s<person>();

  // conn 析构时自动归还
}

// 获取统计信息
auto [total, available, in_use, dynamic] = co_await pool->get_stats();
std::cout << "Pool: " << available << "/" << total << " available\n";
```

---


## 性能分析

### 内存开销对比

| 组件 | 轻量版 | 安全版 | 说明 |
|---|---|---|---|
| **Adapter** | ~56-64 bytes (栈) | ~8 bytes (栈) + ~136-152 bytes (堆) | 安全版使用 shared_ptr + SharedState |
| **堆分配** | 0 | 1 | 安全版每次桥接一次堆分配 |
| **总开销** | ~56-64 bytes | ~144-160 bytes | 安全版约 2.5x |

### CPU 开销对比

| 操作 | 轻量版 | 安全版 | 说明 |
|---|---|---|---|
| **post 调度** | 2x | 4x | 安全版多 2 次 strand post |
| **strand 调度** | 0x | 4x | 安全版所有操作通过 strand |
| **atomic 操作** | 0x | 3x load | 安全版 should_resume 检查 |
| **纯开销** | ~2 μs | ~6 μs | 约 3x |

### 相对于数据库 I/O 的开销

| 场景 | 查询时间 | 轻量版 | 安全版 | 额外开销 | 影响 |
|---|---|---|---|---|---|
| **本地查询** | 100 μs | 2 μs (2%) | 6 μs (6%) | 4 μs | ~4% |
| **远程查询** | 1 ms | 2 μs (0.2%) | 6 μs (0.6%) | 4 μs | ~0.4% |
| **远程查询** | 10 ms | 2 μs (0.02%) | 6 μs (0.06%) | 4 μs | ~0.04% |

**结论**：对于绝大多数实际应用场景（查询时间 > 100 μs），安全版的额外开销相对于数据库 I/O 可以忽略。

---

## 使用指南

### 基础使用

#### 1. 直接在 ASIO 协程中使用

```cpp
asio::awaitable<void> handler() {
  auto executor = co_await asio::this_coro::executor;
  dbng<mysql_async> db(executor);

  // 连接数据库
  co_await db.connect("127.0.0.1", "root", "", "mydb");

  // 插入数据
  co_await db.insert(person{0, "Alice", 25});

  // 查询数据
  auto rows = co_await db.select(all)
                  .from<person>()
                  .where(col(&person::age) > 18)
                  .collect();

  for (const auto& p : rows) {
    std::cout << p.name << ", " << p.age << "\n";
  }
}
```

#### 2. 在 async_simple::Lazy 中使用（通过 mysql_async_session）

```cpp
async_simple::coro::Lazy<void> business_logic() {
  db_wrapper::mysql_async_session db;

  // 连接数据库
  co_await db.connect("127.0.0.1", "root", "", "mydb");

  // 便捷接口直接返回 Lazy
  co_await db.insert(person{0, "Bob", 30});

  // 链式接口通过 raw() + await() 桥接
  auto rows = co_await db.await(
      db.raw()
        .select(all)
        .from<person>()
        .where(col(&person::name).param())
        .collect(std::string("Bob"))
  );

  // 使用安全版适配器（多线程场景）
  auto rows2 = co_await db.await_safe(
      db.raw()
        .select(all)
        .from<person>()
        .collect()
  );
}
```

### 使用连接池

```cpp
async_simple::coro::Lazy<void> with_pool() {
  auto executor = coro_io::get_global_executor()->get_asio_executor();

  // 创建连接池
  auto pool = std::make_shared<async_connection_pool<mysql_async>>(executor);

  // 初始化 8 个连接
  bool ok = co_await pool->init(8, "127.0.0.1", "root", "", "mydb");
  if (!ok) {
    std::cerr << "Pool init failed\n";
    co_return;
  }

  // 获取连接
  auto conn = co_await pool->get(std::chrono::seconds(5));
  if (!conn) {
    std::cerr << "Pool exhausted\n";
    co_return;
  }

  // 使用连接
  co_await conn->insert(person{0, "Charlie", 35});
  auto rows = co_await conn->query_s<person>();

  // conn 析构时自动归还
}
```

### 事务处理

```cpp
async_simple::coro::Lazy<void> transaction_example() {
  db_wrapper::mysql_async_session db;
  co_await db.connect("127.0.0.1", "root", "", "mydb");

  // 开始事务
  co_await db.raw().begin();

  try {
    co_await db.insert(person{0, "Dave", 40});
    co_await db.insert(person{0, "Eve", 45});

    // 提交事务
    co_await db.raw().commit();
  } catch (const std::exception& e) {
    // 回滚事务
    co_await db.raw().rollback();
    std::cerr << "Transaction failed: " << e.what() << "\n";
  }
}
```

### 复杂查询示例

```cpp
async_simple::coro::Lazy<void> complex_query() {
  db_wrapper::mysql_async_session db;
  co_await db.connect("127.0.0.1", "root", "", "mydb");

  // 聚合查询
  auto stats = co_await db.await(
      db.raw()
        .select(col(&person::age), count())
        .from<person>()
        .group_by(col(&person::age))
        .having(count() > 1)
        .collect()
  );

  // 参数化查询
  auto young_people = co_await db.await(
      db.raw()
        .select(all)
        .from<person>()
        .where(col(&person::age) < token && col(&person::name).param())
        .order_by(col(&person::age).asc())
        .limit(token)
        .collect(30, "A%", 10)  // age < 30, name LIKE 'A%', LIMIT 10
  );

  // scalar 查询
  auto avg_age = co_await db.await(
      db.raw()
        .select(avg(col(&person::age)))
        .from<person>()
        .scalar()
  );
}
```

### 取消操作（安全版）

```cpp
async_simple::coro::Lazy<void> cancellable_query() {
  db_wrapper::mysql_async_session db;
  co_await db.connect("127.0.0.1", "root", "", "mydb");

  // 使用安全版适配器
  auto awaiter = db.await_safe(
      db.raw().execute("SELECT SLEEP(10)")
  );

  // 获取取消句柄
  auto cancel_handle = awaiter.get_cancellation_handle();

  // 在另一个控制路径中取消
  // cancel_handle.cancel(asio::cancellation_type::terminal);

  try {
    co_await std::move(awaiter);
  } catch (const std::system_error& e) {
    if (e.code() == asio::error::operation_aborted) {
      std::cout << "Query cancelled\n";
    }
  }
}
```

---

## 设计边界与限制

### 轻量版适配器的约束

当前轻量版 `AsioAwaitableAdapter` 在以下条件下成立：

| 条件 | 要求 | 若突破 |
|---|---|---|
| **单线程 executor** | 同一个 `io_context` 只由一个线程 `run()` | 需要使用安全版 |
| **父协程不中途销毁** | awaiter 存于父协程帧，父协程在等待期间不被销毁 | 需要使用安全版 |
| **不需要取消操作** | 不支持 | 需要使用安全版 |

### 安全版适配器的限制

**async_simple executor 生命周期**：

传入 `coAwait()` 的 `async_simple::Executor*` 指针必须在整个异步操作期间保持有效。

```cpp
// ⚠️ 错误示例
{
  async_simple::executors::SimpleExecutor executor;
  auto awaiter = from_asio_safe(query(), asio_executor);
  co_await std::move(awaiter).coAwait(&executor);
  // executor 析构，但异步操作可能还在进行
}

// ✅ 正确示例
async_simple::executors::SimpleExecutor executor;  // 生命周期足够长
auto awaiter = from_asio_safe(query(), asio_executor);
co_await std::move(awaiter).coAwait(&executor);
```

**取消语义**：

- 取消请求不保证立即生效，取决于底层 ASIO 操作是否支持对应的 `cancellation_type`
- MySQL 查询通常不支持中途取消（除非使用 `KILL QUERY`）
- 取消主要用于超时控制和资源清理

### MySQL 协议限制

**认证插件支持**：

当前仅支持：
- `mysql_native_password`
- `caching_sha2_password`

不支持：
- `sha256_password`（需要 RSA 公钥）
- `auth_socket`
- 其他第三方插件

**字符集**：

默认使用 `utf8_general_ci` (collation_id = 33)，不支持运行时切换字符集。

**Prepared Statement**：

当前使用文本协议 + 客户端参数替换，不使用服务端 Prepared Statement。

优点：
- 实现简单
- 无需管理 statement 生命周期

缺点：
- 每次查询都需要解析 SQL
- 无法利用服务端查询缓存

### 性能考虑

**何时使用轻量版**：
- 单线程 `io_context::run()`
- 父协程生命周期明确
- 不需要取消操作
- 追求极致性能（高频短查询）

**何时使用安全版**：
- 多线程 `io_context::run()`
- awaiter 可能提前销毁
- 需要取消操作
- 查询时间 > 100 μs（开销可忽略）

**连接池配置建议**：

| 场景 | 池大小 | 动态扩容 | 说明 |
|---|---|---|---|
| **低并发** | 2-4 | 关闭 | 减少资源占用 |
| **中并发** | 8-16 | 开启 | 平衡性能和资源 |
| **高并发** | 32-64 | 开启 | 最大化吞吐量 |

`cfg/ormpp.cfg` 中的 `db_conn_num` 可作为默认连接池大小。功能测试中较小的连接数足够，
容量测试和生产配置应根据 MySQL 服务端能力、业务 SQL 耗时和 P95/P99 延迟目标单独压测。
业务协程并发数大于连接池容量时，多出的请求会等待连接；这会提高吞吐利用率，但也会拉长尾延迟。

---

## 总结

### 核心设计原则

1. **零依赖**：完全自实现 MySQL 协议，不依赖 libmysqlclient
2. **真异步**：所有 I/O 都是非阻塞的 ASIO 协程
3. **类型安全**：通过 traits 系统统一同步/异步接口
4. **灵活性**：既支持链式 API，也支持原始 SQL
5. **性能优化**：提供轻量版和安全版适配器，按需选择

### 适用场景

**最佳场景**：
- 单线程异步服务器（如 HTTP 服务器处理数据库请求）
- 高并发低延迟应用
- 结构化并发（协程生命周期明确）

**不适合场景**：
- 需要复杂的取消/超时控制
- 需要使用非标准认证插件
- 需要服务端 Prepared Statement 的性能优化

### 未来改进方向

1. **支持更多认证插件**
2. **实现服务端 Prepared Statement**
3. **支持更细粒度的取消控制**
4. **支持字符集运行时切换**
5. **支持 PostgreSQL 等其他数据库**

---

## 参考资料

- [MySQL Client/Server Protocol](https://dev.mysql.com/doc/dev/mysql-server/latest/PAGE_PROTOCOL.html)
- [ASIO Documentation](https://think-async.com/Asio/asio-1.28.0/doc/)
- [async_simple Documentation](https://github.com/alibaba/async_simple)
- [ormpp GitHub Repository](https://github.com/qicosmos/ormpp)

---

**文档版本**：1.0
**最后更新**：2024-12
**作者**：ormpp 开发团队
