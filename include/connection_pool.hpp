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
  template <typename... Args>
  void init(int maxsize, Args &&...args) {
    std::call_once(flag_, &connection_pool<DB>::template init_impl<Args...>,
                   this, maxsize, std::forward<Args>(args)...);
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
  template <typename... Args>
  void init_impl(int maxsize, Args &&...args) {
    args_ = std::make_tuple(std::forward<Args>(args)...);

    for (int i = 0; i < maxsize; ++i) {
      auto conn = std::make_shared<DB>();
      if (conn->connect(std::forward<Args>(args)...)) {
        pool_.push_back(conn);
      }
      else {
        throw std::invalid_argument("init failed");
      }
    }
  }

  auto create_connection() {
    auto conn = std::make_shared<DB>();
    auto fn = [conn](auto... targs) {
      return conn->connect(targs...);
    };

    return std::apply(fn, args_) ? conn : nullptr;
  }

  connection_pool() = default;
  ~connection_pool() = default;
  connection_pool(const connection_pool &) = delete;
  connection_pool &operator=(const connection_pool &) = delete;

  std::deque<std::shared_ptr<DB>> pool_;
  std::mutex mutex_;
  std::condition_variable condition_;
  std::once_flag flag_;
  std::tuple<const char *, const char *, const char *, const char *, int, int>
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