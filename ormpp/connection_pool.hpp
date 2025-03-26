//
// Created by qiyu on 12/14/17.
//

#ifndef ORMPP_CONNECTION_POOL_HPP
#define ORMPP_CONNECTION_POOL_HPP

#include <chrono>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <tuple>

namespace ormpp {
template <typename DB>
class connection_pool {
 public:
  static connection_pool<DB> &instance() {
    static connection_pool<DB> instance;
    return instance;
  }

  // call_once
  void init(int maxsize, const std::string &host = "",
            const std::string &user = "", const std::string &passwd = "",
            const std::string &db = "", const std::optional<int> &timeout = {},
            const std::optional<int> &port = {}) {
    std::call_once(flag_, &connection_pool<DB>::init_impl, this, maxsize, host,
                   user, passwd, db, timeout, port);
  }

  std::shared_ptr<DB> get() {
    std::unique_lock<std::mutex> lock(mutex_);

    while (pool_.empty()) {
      if (condition_.wait_for(lock, std::chrono::seconds(3)) ==
          std::cv_status::timeout) {
        // timeout
        return nullptr;
      }
    }

    auto conn = pool_.front();
    pool_.pop_front();
    lock.unlock();

    if (conn == nullptr || !conn->ping()) {
      return create_connection();
    }

    // check timeout, idle time shuold less than 8 hours
    auto now = std::chrono::system_clock::now();
    auto last = conn->get_latest_operate_time();
    auto mins =
        std::chrono::duration_cast<std::chrono::minutes>(now - last).count();
    if ((mins - 6 * 60) > 0) {
      return create_connection();
    }

    conn->update_operate_time();
    return conn;
  }

  void return_back(std::shared_ptr<DB> conn) {
    if (conn == nullptr || conn->has_error()) {
      conn = create_connection();
    }
    std::unique_lock<std::mutex> lock(mutex_);
    pool_.push_back(conn);
    lock.unlock();
    condition_.notify_one();
  }

 private:
  void init_impl(int maxsize, const std::string &host, const std::string &user,
                 const std::string &passwd, const std::string &db,
                 const std::optional<int> &timeout,
                 const std::optional<int> &port) {
    args_ = std::make_tuple(host, user, passwd, db, timeout, port);

    for (int i = 0; i < maxsize; ++i) {
      auto conn = std::make_shared<DB>();
      if (conn->connect(args_)) {
        pool_.push_back(conn);
      }
      else {
        throw std::invalid_argument("init failed");
      }
    }
  }

  auto create_connection() {
    auto conn = std::make_shared<DB>();
    return conn->connect(args_) ? conn : nullptr;
  }

  connection_pool() = default;
  ~connection_pool() = default;
  connection_pool(const connection_pool &) = delete;
  connection_pool &operator=(const connection_pool &) = delete;

  std::mutex mutex_;
  std::once_flag flag_;
  std::condition_variable condition_;
  std::deque<std::shared_ptr<DB>> pool_;
  std::tuple<std::string, std::string, std::string, std::string,
             std::optional<int>, std::optional<int>>
      args_;
};

template <typename DB>
struct conn_guard {
  conn_guard(std::shared_ptr<DB> con) : conn_(con) {}
  ~conn_guard() { connection_pool<DB>::instance().return_back(conn_.lock()); }

 private:
  std::weak_ptr<DB> conn_;
};
}  // namespace ormpp

#endif  // ORMPP_CONNECTION_POOL_HPP