#pragma once

#include <async_simple/coro/Lazy.h>
#include <async_simple/coro/SyncAwait.h>

#include <cinatra/ylt/coro_io/io_context_pool.hpp>
#include <cstdint>
#include <optional>
#include <ormpp/dbng.hpp>
#include <ormpp/mysql_async.hpp>
#include <string>
#include <utility>

#include "asio_async_simple_adapter.hpp"

namespace db_wrapper {

class mysql_async_session {
 public:
  using db_type = ormpp::dbng<ormpp::mysql_async>;

  mysql_async_session()
      : executor_(coro_io::get_global_executor()->get_asio_executor()),
        db_(executor_) {}

  explicit mysql_async_session(asio::any_io_executor executor)
      : executor_(std::move(executor)), db_(executor_) {}

  db_type &raw() { return db_; }
  const db_type &raw() const { return db_; }

  asio::any_io_executor executor() const { return executor_; }

  template <typename T>
  auto await(asio::awaitable<T, asio::any_io_executor> awaitable) {
    return adapter::from_asio(std::move(awaitable), executor_);
  }

  async_simple::coro::Lazy<bool> connect(const std::string &host,
                                         const std::string &user,
                                         const std::string &passwd,
                                         const std::string &database,
                                         const std::optional<int> &timeout = {},
                                         const std::optional<int> &port = {}) {
    co_return co_await await(
        db_.connect(host, user, passwd, database, timeout, port));
  }

  async_simple::coro::Lazy<bool> execute(std::string sql) {
    co_return co_await await(db_.execute(sql));
  }

  template <typename T, typename... Args>
  async_simple::coro::Lazy<int> insert(const T &value, Args &&...args) {
    co_return co_await await(db_.insert(value, std::forward<Args>(args)...));
  }

  template <typename T, typename... Args>
  async_simple::coro::Lazy<std::uint64_t> get_insert_id_after_insert(
      const T &value, Args &&...args) {
    co_return co_await await(
        db_.get_insert_id_after_insert(value, std::forward<Args>(args)...));
  }

  int get_last_affect_rows() { return db_.get_last_affect_rows(); }
  bool has_error() { return db_.has_error(); }
  std::string get_last_error() const { return db_.get_last_error(); }

 private:
  asio::any_io_executor executor_;
  db_type db_;
};

using mysql_async_wrapper = mysql_async_session;

template <typename Lazy>
decltype(auto) sync_wait(Lazy &&lazy) {
  return async_simple::coro::syncAwait(
      std::forward<Lazy>(lazy).via(coro_io::get_global_executor()));
}

}  // namespace db_wrapper
