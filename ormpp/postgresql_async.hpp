#pragma once

#ifdef ORMPP_ENABLE_PG_ASYNC

#include <libpq-fe.h>

#include <algorithm>
#include <asio.hpp>
#include <asio/experimental/awaitable_operators.hpp>
#include <charconv>
#include <cstring>
#include <deque>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include "async_traits.hpp"
#include "entity.hpp"
#include "query.hpp"
#include "type_mapping.hpp"

namespace ormpp {
namespace detail::postgresql_async {

using result_ptr = std::unique_ptr<PGresult, decltype(&PQclear)>;

struct parameter {
  std::optional<std::string> value;
  Oid type = 0;
  int format = 0;
};
using parameters = std::vector<parameter>;

template <typename T>
parameter make_parameter(const T& value) {
  using U = std::remove_cvref_t<T>;
  if constexpr (is_optional_v<U>::value) {
    return value ? make_parameter(*value) : parameter{};
  }
  else if constexpr (std::is_same_v<U, std::nullptr_t> ||
                     std::is_same_v<U, std::nullopt_t>) {
    return {};
  }
  else if constexpr (std::is_same_v<U, blob>) {
    return {std::string(value.begin(), value.end()), 17, 1};  // BYTEAOID
  }
  else if constexpr (std::is_enum_v<U>) {
    return parameter{
        std::to_string(static_cast<std::underlying_type_t<U>>(value))};
  }
  else if constexpr (std::is_same_v<U, char>) {
    return parameter{std::string(1, value)};
  }
  else if constexpr (std::is_integral_v<U>) {
    return parameter{std::to_string(value)};
  }
  else if constexpr (std::is_floating_point_v<U>) {
    char buffer[128];
    auto [end, ec] = std::to_chars(buffer, buffer + sizeof(buffer), value,
                                   std::chars_format::general,
                                   std::numeric_limits<U>::max_digits10);
    if (ec != std::errc{}) {
      throw std::runtime_error(
          "postgresql_async: invalid floating point value");
    }
    return parameter{std::string(buffer, end)};
  }
  else {
    std::string text;
    if constexpr (is_db_text_type_v<U> || iguana::array_v<U>) {
      text.assign(value.data(), value.size());
      if constexpr (iguana::array_v<U>) {
        text.resize(text.find('\0') == std::string::npos ? text.size()
                                                         : text.find('\0'));
      }
    }
    else if constexpr (std::is_array_v<U>) {
      text.assign(value, std::find(value, value + std::extent_v<U>, '\0'));
    }
    else {
      text = std::string(value);
    }
    return parameter{std::move(text)};
  }
}

template <typename... Args>
parameters make_parameters(const Args&... args) {
  return {make_parameter(args)...};
}

template <typename T>
void assign_value(T& value, PGresult* result, int row, int col,
                  std::deque<std::string>& storage) {
  using U = std::remove_cvref_t<T>;
  if (col >= PQnfields(result)) {
    throw std::runtime_error("postgresql_async: row column count mismatch");
  }
  if (PQgetisnull(result, row, col)) {
    if constexpr (std::is_array_v<U>) {
      std::memset(value, 0, sizeof(U));
    }
    else {
      value = U{};
    }
    return;
  }
  const std::string_view text(PQgetvalue(result, row, col),
                              PQgetlength(result, row, col));
  if constexpr (is_optional_v<U>::value) {
    typename U::value_type item{};
    assign_value(item, result, row, col, storage);
    value = std::move(item);
  }
  else if constexpr (std::is_same_v<U, bool>) {
    value = text == "t" || text == "true" || text == "1";
  }
  else if constexpr (std::is_same_v<U, char>) {
    value = text.empty() ? '\0' : text.front();
  }
  else if constexpr (std::is_enum_v<U>) {
    std::underlying_type_t<U> item{};
    assign_value(item, result, row, col, storage);
    value = static_cast<U>(item);
  }
  else if constexpr (std::is_integral_v<U>) {
    auto [end, ec] =
        std::from_chars(text.data(), text.data() + text.size(), value);
    if (ec != std::errc{} || end != text.data() + text.size()) {
      throw std::runtime_error("postgresql_async: invalid integral result");
    }
  }
  else if constexpr (std::is_floating_point_v<U>) {
    value = static_cast<U>(std::stold(std::string(text)));
  }
  else if constexpr (std::is_same_v<U, std::string>) {
    value = text;
  }
  else if constexpr (std::is_same_v<U, std::string_view>) {
    storage.emplace_back(text);
    value = storage.back();
  }
  else if constexpr (is_db_text_type_v<U>) {
    value.value = text;
  }
  else if constexpr (std::is_same_v<U, blob>) {
    std::size_t length = 0;
    auto decoded = std::unique_ptr<unsigned char, decltype(&PQfreemem)>(
        PQunescapeBytea(reinterpret_cast<const unsigned char*>(text.data()),
                        &length),
        &PQfreemem);
    if (!decoded) {
      throw std::bad_alloc();
    }
    value.assign(decoded.get(), decoded.get() + length);
  }
  else if constexpr (iguana::array_v<U>) {
    std::memset(value.data(), 0, value.size());
    std::memcpy(value.data(), text.data(), std::min(value.size(), text.size()));
  }
  else if constexpr (iguana::c_array_v<U>) {
    std::memset(value, 0, sizeof(U));
    std::memcpy(value, text.data(), std::min(sizeof(U), text.size()));
  }
  else {
    static_assert(!sizeof(U),
                  "postgresql_async: unsupported result field type");
  }
}

template <typename T>
void map_columns(T& value, PGresult* result, int row, int& col,
                 std::deque<std::string>& storage) {
  if constexpr (iguana::ylt_refletable_v<T>) {
    ylt::reflection::for_each(value, [&](auto& field, auto, auto) {
      assign_value(field, result, row, col++, storage);
    });
  }
  else if constexpr (iguana::is_tuple<T>::value) {
    std::apply(
        [&](auto&... field) {
          (map_columns(field, result, row, col, storage), ...);
        },
        value);
  }
  else {
    assign_value(value, result, row, col++, storage);
  }
}

template <typename T, typename... Args>
std::string create_table_sql(const Args&... args) {
  std::string auto_key(get_auto_key<T>());
  auto primary_keys = get_conflict_keys<T>(DBType::postgresql);
  std::set<std::string> not_null;
  std::vector<std::set<std::string>> unique;
  auto constraint = [&](const auto& arg) {
    using U = std::decay_t<decltype(arg)>;
    if constexpr (std::is_same_v<U, ormpp_auto_key>) {
      auto_key = arg.fields;
    }
    else if constexpr (std::is_same_v<U, ormpp_key>) {
      if (primary_keys.empty()) {
        primary_keys.push_back(arg.fields);
      }
    }
    else if constexpr (std::is_same_v<U, ormpp_not_null>) {
      not_null.insert(arg.fields.begin(), arg.fields.end());
    }
    else if constexpr (std::is_same_v<U, ormpp_unique>) {
      unique.push_back(arg.fields);
    }
  };
  (constraint(args), ...);
  const auto types = get_type_names<T>(DBType::postgresql);
  std::string sql = "CREATE TABLE IF NOT EXISTS ";
  sql.append(get_short_struct_name<T>()).append("(");
  T sample{};
  ylt::reflection::for_each(sample, [&](auto&, auto name, std::size_t index) {
    // Match the shared PostgreSQL CRUD/builders' unquoted identifiers,
    // including PostgreSQL's case folding for reflected C++ member names.
    sql.append(name).append(" ");
    if (name == auto_key) {
      sql.append(types[index] == "bigint" ? "bigserial" : "serial");
    }
    else {
      sql.append(types[index]);
    }
    if (not_null.count(std::string(name))) {
      sql.append(" NOT NULL");
    }
    sql.push_back(',');
  });
  auto append_keys = [&](std::string_view kind, const auto& keys) {
    if (keys.empty()) {
      return;
    }
    sql.append(kind).append("(");
    for (const auto& key : keys) {
      sql.append(key).push_back(',');
    }
    sql.back() = ')';
    sql.push_back(',');
  };
  if (!auto_key.empty()) {
    primary_keys = {auto_key};
  }
  append_keys("PRIMARY KEY", primary_keys);
  for (const auto& keys : unique) {
    append_keys("UNIQUE", keys);
  }
  sql.back() = ')';
  return sql;
}

}  // namespace detail::postgresql_async

class postgresql_async {
 public:
  using executor_type = asio::any_io_executor;
  template <typename T>
  using awaitable = asio::awaitable<T, executor_type>;
  static constexpr DBType db_type_v = DBType::postgresql;

  // One io_context worker per thread. Construct, use and destroy the connection
  // on that worker; callers keep explicit transactions on an exclusive lease.
  explicit postgresql_async(executor_type executor)
      : state_(std::make_shared<state>(std::move(executor))) {}
  explicit postgresql_async(asio::io_context& ctx)
      : postgresql_async(ctx.get_executor()) {}
  postgresql_async(const postgresql_async&) = delete;
  postgresql_async& operator=(const postgresql_async&) = delete;
  postgresql_async(postgresql_async&&) = default;
  postgresql_async& operator=(postgresql_async&&) = default;

  executor_type get_executor() const { return state_->executor; }

  // Nonblocking idle-connection probe; consume EOF/TLS close notifications
  // without issuing SQL. Call on the owning worker, outside an active
  // operation.
  bool is_open() const {
    auto* connection = state_->connection.get();
    if (!connection || state_->in_progress ||
        PQstatus(connection) != CONNECTION_OK || !PQconsumeInput(connection)) {
      return false;
    }
    // An idle FATAL message can be buffered while PQstatus still says OK.
    // Parse the notification, then consume its trailing EOF/TLS shutdown.
    return !PQisBusy(connection) && PQconsumeInput(connection) != 0 &&
           PQstatus(connection) == CONNECTION_OK;
  }

  bool has_error() const { return !state_->error.empty(); }
  void reset_error() { state_->set_error({}); }
  void set_last_error(std::string error) {
    state_->set_error(std::move(error));
  }
  std::string get_last_error() const { return state_->error; }
  int get_last_affect_rows() const { return state_->affected; }
  void set_enable_transaction(bool enable) { state_->transaction = enable; }

  awaitable<bool> connect(
      const std::tuple<std::string, std::string, std::string, std::string,
                       std::optional<int>, std::optional<int>>& params) {
    return std::apply(
        [&](const auto&... args) {
          return connect(args...);
        },
        params);
  }

  awaitable<bool> connect(const std::string& host, const std::string& user = "",
                          const std::string& password = "",
                          const std::string& database = "",
                          const std::optional<int>& timeout = {},
                          const std::optional<int>& port = {}) {
    // This function is deliberately not a coroutine: own every input before
    // returning a lazy awaitable, including string_views passed to query_s.
    return launch(
        [host, user, password, database, timeout,
         port](state& s) -> awaitable<bool> {
          s.close();
          if (timeout.value_or(0) <= 0) {
            if (auto error =
                    co_await s.connect(host, user, password, database, port)) {
              std::rethrow_exception(error);
            }
          }
          else {
            using asio::experimental::awaitable_operators::operator||;
            asio::steady_timer timer(s.executor,
                                     std::chrono::seconds(*timeout));
            // operator|| joins both branches after cancelling the loser. In
            // particular, no timer callback can access a later connection.
            auto outcome =
                co_await (s.connect(host, user, password, database, port) ||
                          timer.async_wait(asio::use_awaitable));
            if (outcome.index() == 1) {
              s.close();
              throw std::runtime_error("postgresql_async: connect timed out");
            }
            if (auto error = std::get<0>(outcome)) {
              std::rethrow_exception(error);
            }
          }
          co_return true;
        },
        false);
  }

  awaitable<bool> disconnect() {
    return launch(
        [](state& s) -> awaitable<bool> {
          s.close();
          co_return true;
        },
        false);
  }

  awaitable<bool> ping() { return execute("SELECT 1"); }

  template <typename T, typename... Args>
  awaitable<bool> create_datatable(Args&&... args) {
    return execute(detail::postgresql_async::create_table_sql<T>(args...));
  }

  template <typename T, typename... Args>
  awaitable<int> insert(const T& value, Args&&... args) {
    return write<int, T>(value, OptType::insert, args...);
  }
  template <typename T, typename... Args>
  awaitable<int> insert(const std::vector<T>& values, Args&&... args) {
    return write<int, T>(values, OptType::insert, args...);
  }
  template <typename T, typename... Args>
  awaitable<int> replace(const T& value, Args&&... args) {
    return write<int, T>(value, OptType::replace, args...);
  }
  template <typename T, typename... Args>
  awaitable<int> replace(const std::vector<T>& values, Args&&... args) {
    return write<int, T>(values, OptType::replace, args...);
  }

  template <auto... Members, typename T, typename... Args>
  awaitable<int> update(const T& value, Args&&... args) {
    return write<int, T, Members...>(value, OptType::update, args...);
  }
  template <auto... Members, typename T, typename... Args>
  awaitable<int> update(const std::vector<T>& values, Args&&... args) {
    return write<int, T, Members...>(values, OptType::update, args...);
  }

  template <typename T, typename... Args>
  awaitable<std::uint64_t> get_insert_id_after_insert(const T& value,
                                                      Args&&... args) {
    return write<std::uint64_t, T>(value, OptType::insert, args...);
  }
  template <typename T, typename... Args>
  awaitable<std::uint64_t> get_insert_id_after_insert(
      const std::vector<T>& values, Args&&... args) {
    return write<std::uint64_t, T>(values, OptType::insert, args...);
  }

  template <typename T, typename... Args>
  awaitable<std::uint64_t> delete_records_s(const std::string& condition = "",
                                            Args&&... args) {
    return launch(
        [sql = generate_delete_sql<T>(db_type_v, condition),
         params = detail::postgresql_async::make_parameters(args...)](
            state& s) -> awaitable<std::uint64_t> {
          co_await s.command(sql, params);
          co_return s.affected;
        },
        std::uint64_t{0});
  }

  template <typename T, typename... Args>
  awaitable<std::vector<T>> query_s(const std::string& condition = "",
                                    Args&&... args) {
    return query_rows<T>(false, condition, std::forward<Args>(args)...);
  }

  // Explicit single-statement query path. Cached statements belong to this
  // connection and never retain parameter values or result rows.
  template <typename T, typename... Args>
  awaitable<std::vector<T>> query_prepared_s(const std::string& condition = "",
                                             Args&&... args) {
    return query_rows<T>(true, condition, std::forward<Args>(args)...);
  }

  template <typename T, typename... Args>
  awaitable<bool> delete_records(Args&&... args) {
    return execute(
        generate_delete_sql<T>(db_type_v, std::forward<Args>(args)...));
  }
  template <typename T, typename... Args>
  awaitable<std::vector<T>> query(Args&&... args) {
    return query_s<T>(
        generate_query_sql<T>(db_type_v, std::forward<Args>(args)...));
  }

  template <typename... Args>
  auto select(Args... args) {
    return ormpp::select(this, args...);
  }
  auto select(all_t) { return ormpp::select_all(this); }
  auto select_all() { return ormpp::select_all(this); }
  template <typename T>
  auto make_update() {
    return ormpp::make_update_builder<T>(this);
  }
  template <typename T>
  auto make_delete() {
    return ormpp::make_delete_builder<T>(this);
  }
  template <typename T>
  auto make_create_table() {
    return ormpp::make_create_table_builder<T>(this);
  }
  template <typename T>
  auto make_alter_table() {
    return ormpp::make_alter_table_builder<T>(this);
  }

  awaitable<bool> execute(const std::string& sql) {
    return launch(
        [sql](state& s) -> awaitable<bool> {
          co_await s.command(sql);
          co_return true;
        },
        false);
  }
  awaitable<bool> begin() { return execute("BEGIN"); }
  awaitable<bool> commit() { return execute("COMMIT"); }
  awaitable<bool> rollback() { return execute("ROLLBACK"); }

 private:
  using parameters = detail::postgresql_async::parameters;
  using result_ptr = detail::postgresql_async::result_ptr;

  struct state {
    executor_type executor;
    std::unique_ptr<PGconn, decltype(&PQfinish)> connection{nullptr, &PQfinish};
    std::deque<std::string> string_storage;
    std::string error;
    int affected = 0;
    bool transaction = true;
    bool in_progress = false;
    using statement_key = std::pair<std::string, std::vector<Oid>>;
    std::map<statement_key, std::string> prepared_statements;
    std::size_t next_statement = 0;
    static constexpr std::size_t max_prepared_statements = 128;

    explicit state(executor_type ex) : executor(std::move(ex)) {}
    void set_error(std::string message) { error = std::move(message); }
    void close() {
      connection.reset();
      prepared_statements.clear();
      next_statement = 0;
    }
    std::runtime_error connection_error() const {
      return std::runtime_error(connection ? PQerrorMessage(connection.get())
                                           : "postgresql_async: not connected");
    }

    awaitable<void> wait(asio::socket_base::wait_type direction,
                         bool either = false) {
      // libpq retains ownership of the socket and may replace it in
      // connectPoll. Borrow only for this wait and release before any libpq
      // call or close.
      asio::ip::tcp::socket socket(executor);
      int fd = PQsocket(connection.get());
      if (fd < 0) {
        throw connection_error();
      }
      socket.assign(asio::ip::tcp::v4(), fd);
      struct release_socket {
        asio::ip::tcp::socket& socket;
        ~release_socket() {
          asio::error_code ignored;
          socket.release(ignored);
        }
      } release{socket};
      if (either) {
        using asio::experimental::awaitable_operators::operator||;
        co_await (socket.async_wait(asio::socket_base::wait_read,
                                    asio::use_awaitable) ||
                  socket.async_wait(asio::socket_base::wait_write,
                                    asio::use_awaitable));
      }
      else {
        co_await socket.async_wait(direction, asio::use_awaitable);
      }
    }

    awaitable<std::exception_ptr> connect(std::string host, std::string user,
                                          std::string password,
                                          std::string database,
                                          std::optional<int> port) {
      try {
        // Supplying hostaddr keeps libpq's polling API from doing blocking DNS.
        // Do not silently inherit PGHOST/PGSERVICE when no host was supplied.
        if (host.empty() || host.find(',') != std::string::npos) {
          throw std::runtime_error(
              "postgresql_async: connect requires a single non-empty host");
        }
        std::string addresses;
        std::string hosts;
        auto service = std::to_string(port.value_or(5432));
        if (!host.empty() && host.front() != '/' && host.front() != '@') {
          asio::ip::tcp::resolver resolver(executor);
          auto endpoints = co_await resolver.async_resolve(host, service,
                                                           asio::use_awaitable);
          for (const auto& endpoint : endpoints) {
            if (!addresses.empty()) {
              addresses.push_back(',');
              hosts.push_back(',');
            }
            addresses.append(endpoint.endpoint().address().to_string());
            hosts.append(host);
          }
        }
        const char* keys[] = {"host",   "hostaddr", "user", "password",
                              "dbname", "port",     nullptr};
        const char* values[] = {hosts.empty() ? host.c_str() : hosts.c_str(),
                                addresses.c_str(),
                                user.c_str(),
                                password.c_str(),
                                database.c_str(),
                                service.c_str(),
                                nullptr};
        connection.reset(PQconnectStartParams(keys, values, 0));
        if (!connection || PQstatus(connection.get()) == CONNECTION_BAD) {
          throw connection_error();
        }
        auto status = PGRES_POLLING_WRITING;
        while (status != PGRES_POLLING_OK) {
          if (status == PGRES_POLLING_FAILED) {
            throw connection_error();
          }
          if (status == PGRES_POLLING_READING ||
              status == PGRES_POLLING_WRITING) {
            co_await wait(status == PGRES_POLLING_READING
                              ? asio::socket_base::wait_read
                              : asio::socket_base::wait_write);
          }
          status = PQconnectPoll(connection.get());
        }
        if (PQsetnonblocking(connection.get(), 1) != 0) {
          throw connection_error();
        }
        co_return nullptr;
      } catch (...) {
        close();
        co_return std::current_exception();
      }
    }

    // Complete and drain one libpq exchange before another send. SQL errors
    // are returned separately so a drained server error keeps the connection.
    awaitable<result_ptr> read_response(std::string& sql_error) {
      for (;;) {
        int pending = PQflush(connection.get());
        if (pending < 0) {
          throw connection_error();
        }
        if (!pending) {
          break;
        }
        co_await wait(asio::socket_base::wait_write, true);
        if (!PQconsumeInput(connection.get())) {
          throw connection_error();
        }
      }
      result_ptr last(nullptr, &PQclear);
      for (;;) {
        if (!PQconsumeInput(connection.get())) {
          throw connection_error();
        }
        if (PQisBusy(connection.get())) {
          co_await wait(asio::socket_base::wait_read);
          continue;
        }
        result_ptr result(PQgetResult(connection.get()), &PQclear);
        if (!result) {
          break;
        }
        auto status = PQresultStatus(result.get());
        if (status == PGRES_COPY_IN || status == PGRES_COPY_OUT ||
            status == PGRES_COPY_BOTH) {
          throw std::runtime_error("postgresql_async: COPY is not supported");
        }
        if (status != PGRES_COMMAND_OK && status != PGRES_TUPLES_OK &&
            sql_error.empty()) {
          sql_error = PQresultErrorMessage(result.get());
          if (sql_error.empty()) {
            sql_error = "postgresql_async: empty or unsupported command result";
          }
        }
        last = std::move(result);
      }
      co_return last;
    }

    awaitable<result_ptr> command(const std::string& sql,
                                  const parameters& params = {},
                                  bool use_prepared = false) {
      affected = 0;
      if (!connection || PQstatus(connection.get()) != CONNECTION_OK) {
        throw connection_error();
      }
      for (const auto& param : params) {
        if (param.value && param.format == 0 &&
            param.value->find('\0') != std::string::npos) {
          throw std::runtime_error(
              "postgresql_async: NUL in text parameter; use blob");
        }
        if (param.value &&
            param.value->size() >
                static_cast<std::size_t>(std::numeric_limits<int>::max())) {
          throw std::runtime_error("postgresql_async: parameter too large");
        }
      }
#ifdef ORMPP_ENABLE_LOG
      std::cout << sql << std::endl;
#endif
      std::string sql_error;
      result_ptr last(nullptr, &PQclear);
      try {
        int sent;
        if (params.empty() && !use_prepared) {
          sent = PQsendQuery(connection.get(), sql.c_str());
        }
        else {
          std::vector<const char*> values;
          std::vector<Oid> types;
          std::vector<int> lengths, formats;
          for (const auto& param : params) {
            values.push_back(param.value ? param.value->c_str() : nullptr);
            types.push_back(param.type);
            lengths.push_back(
                param.value ? static_cast<int>(param.value->size()) : 0);
            formats.push_back(param.format);
          }
          auto cached = prepared_statements.end();
          if (use_prepared) {
            statement_key key{sql, types};
            cached = prepared_statements.find(key);
            if (cached == prepared_statements.end() &&
                prepared_statements.size() < max_prepared_statements) {
              auto name = "ormpp_ps_" + std::to_string(++next_statement);
              cached = prepared_statements.emplace(std::move(key), name).first;
              if (!PQsendPrepare(connection.get(), name.c_str(), sql.c_str(),
                                 params.size(), types.data())) {
                throw connection_error();
              }
              co_await read_response(sql_error);
              if (!sql_error.empty()) {
                prepared_statements.erase(cached);
                cached = prepared_statements.end();
              }
            }
          }
          if (!sql_error.empty()) {
            // Preparation failed, and its error has already been drained.
            // Do not execute the query again or retain a failed cache entry.
            sent = 1;
          }
          else if (cached != prepared_statements.end()) {
            sent = PQsendQueryPrepared(connection.get(), cached->second.c_str(),
                                       params.size(), values.data(),
                                       lengths.data(), formats.data(), 0);
          }
          else {
            // Bound client/server cache growth. New statements beyond the
            // limit still use a single parameterized protocol exchange.
            sent = PQsendQueryParams(connection.get(), sql.c_str(),
                                     params.size(), types.data(), values.data(),
                                     lengths.data(), formats.data(), 0);
          }
        }
        if (!sent) {
          throw connection_error();
        }
        if (sql_error.empty()) {
          last = co_await read_response(sql_error);
        }
      } catch (...) {
        // A cancelled/failed exchange must never leave unread protocol data for
        // the next operation. wait() has already released the borrowed socket.
        close();
        throw;
      }
      if (!sql_error.empty()) {
        // SQL errors are drained to PQgetResult == nullptr before reuse.
        throw std::runtime_error(sql_error);
      }
      if (last) {
        affected = static_cast<int>(
            std::strtoull(PQcmdTuples(last.get()), nullptr, 10));
      }
      co_return last;
    }
  };

  template <typename F, typename R>
  static awaitable<R> invoke(std::shared_ptr<state> s, F operation, R failure) {
    // A single worker prevents data races, but separate coroutines can still
    // overlap while I/O is suspended. Reject that misuse without queueing,
    // locking, executor migration or touching the active exchange's socket.
    if (s->in_progress) {
      s->set_error("postgresql_async: operation already in progress");
      co_return failure;
    }
    s->in_progress = true;
    struct finish_operation {
      state& s;
      ~finish_operation() { s.in_progress = false; }
    } guard{*s};
    s->set_error({});
    s->affected = 0;
    try {
      co_return co_await operation(*s);
    } catch (const std::exception& e) {
      s->affected = 0;
      s->set_error(e.what());
      co_return failure;
    }
  }

  template <typename F, typename R>
  awaitable<R> launch(F operation, R failure) {
    return invoke(state_, std::move(operation), std::move(failure));
  }

  template <typename T, typename... Args>
  awaitable<std::vector<T>> query_rows(bool use_prepared,
                                       const std::string& condition,
                                       Args&&... args) {
    std::string sql = condition;
    if constexpr (iguana::ylt_refletable_v<T>) {
      if (!contains_select(sql)) {
        sql = generate_query_sql<T>(db_type_v, condition);
      }
    }
    else {
      static_assert(iguana::is_tuple<T>::value,
                    "query_s requires a reflected struct or tuple");
    }
    return launch(
        [sql = std::move(sql), use_prepared,
         params = detail::postgresql_async::make_parameters(args...)](
            state& s) -> awaitable<std::vector<T>> {
          s.string_storage.clear();
          auto result = co_await s.command(sql, params, use_prepared);
          if (!result || PQresultStatus(result.get()) != PGRES_TUPLES_OK) {
            throw std::runtime_error(
                "postgresql_async: query returned no rows");
          }
          std::vector<T> rows;
          rows.reserve(PQntuples(result.get()));
          for (int row = 0; row < PQntuples(result.get()); ++row) {
            T value{};
            int col = 0;
            detail::postgresql_async::map_columns(value, result.get(), row, col,
                                                  s.string_storage);
            if (col != PQnfields(result.get())) {
              throw std::runtime_error(
                  "postgresql_async: row column count mismatch");
            }
            rows.push_back(std::move(value));
          }
          co_return rows;
        },
        std::vector<T>{});
  }

  template <typename T, auto... Members, typename... Args>
  static parameters struct_parameters(const T& value, OptType type,
                                      const Args&... args) {
    parameters params;
    if constexpr (sizeof...(Members) > 0) {
      (params.push_back(detail::postgresql_async::make_parameter(
           ylt::reflection::get<ylt::reflection::index_of<Members>()>(value))),
       ...);
    }
    else {
      ylt::reflection::for_each(value, [&](const auto& field, auto name, auto) {
        if (type != OptType::insert || !is_insert_skipped<T>(name)) {
          params.push_back(detail::postgresql_async::make_parameter(field));
        }
      });
    }
    if constexpr (sizeof...(Args) == 0) {
      if (type == OptType::update) {
        // Match the SQL's conflict-key order, which can differ from field
        // order.
        for (const auto& key : get_conflict_keys<T>(db_type_v)) {
          ylt::reflection::for_each(value, [&](const auto& field, auto name,
                                               auto) {
            if (name == key) {
              params.push_back(detail::postgresql_async::make_parameter(field));
            }
          });
        }
      }
    }
    return params;
  }

  template <typename R, typename T, auto... Members, typename Value,
            typename... Args>
  awaitable<R> write(const Value& value, OptType type, const Args&... args) {
    constexpr bool returning = std::is_same_v<R, std::uint64_t>;
    std::string sql =
        type == OptType::update
            ? generate_update_sql<T, Members...>(db_type_v, args...)
            : generate_insert_sql<T>(db_type_v, type == OptType::insert,
                                     args...);
    if (returning && !std::string_view(get_auto_key<T>()).empty()) {
      sql += " RETURNING " + std::string(get_auto_key<T>());
    }
    std::vector<parameters> batches;
    if constexpr (std::is_same_v<Value, std::vector<T>>) {
      for (const auto& item : value) {
        batches.push_back(
            struct_parameters<T, Members...>(item, type, args...));
      }
    }
    else {
      batches.push_back(struct_parameters<T, Members...>(value, type, args...));
    }
    return launch(
        [sql = std::move(sql), batches = std::move(batches), returning,
         has_key = !std::string_view(get_auto_key<T>()).empty()](
            state& s) -> awaitable<R> {
          if (sql.empty()) {
            throw std::runtime_error(
                "update requires a conflict key or where condition");
          }
          if (returning && !has_key) {
            throw std::runtime_error(
                "postgresql_async: insert id requires an auto key");
          }
          bool transaction = s.transaction && batches.size() > 1;
          if (transaction) {
            co_await s.command("BEGIN");
          }
          std::exception_ptr error;
          std::uint64_t id = 0;
          int affected = 0;
          try {
            for (const auto& params : batches) {
              auto result = co_await s.command(sql, params);
              affected += s.affected;
              if (returning && result && PQntuples(result.get()) > 0) {
                int col = 0;
                detail::postgresql_async::map_columns(id, result.get(), 0, col,
                                                      s.string_storage);
              }
            }
            if (transaction) {
              co_await s.command("COMMIT");
            }
          } catch (...) {
            error = std::current_exception();
          }
          if (error) {
            if (transaction && s.connection) {
              co_await s.command("ROLLBACK");
            }
            std::rethrow_exception(error);
          }
          s.affected = affected;
          co_return returning ? static_cast<R>(id) : static_cast<R>(affected);
        },
        returning ? R{0} : static_cast<R>(std::numeric_limits<int>::min()));
  }

  std::shared_ptr<state> state_;
};

template <>
struct db_execution_traits<postgresql_async> {
  static constexpr bool is_async = true;
  template <typename T>
  using awaitable_type = postgresql_async::awaitable<T>;
};

}  // namespace ormpp

#endif  // ORMPP_ENABLE_PG_ASYNC
