#ifndef ORMPP_ASYNC_CONNECTION_POOL_HPP
#define ORMPP_ASYNC_CONNECTION_POOL_HPP

#include <algorithm>
#include <asio.hpp>
#include <chrono>
#include <cstddef>
#include <deque>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <utility>

#include "async_traits.hpp"

namespace ormpp {

// Pool configuration options
struct pool_options {
  bool enable_dynamic_expansion =
      false;  // Allow temporary connections when pool is full
  size_t max_dynamic_connections = 10;  // Max temporary connections
  bool log_pool_exhaustion = true;      // Log when pool is exhausted
};

// Async connection pool for databases that support async operations
template <typename DB>
  requires is_async_db_v<DB>
class async_connection_pool
    : public std::enable_shared_from_this<async_connection_pool<DB>> {
 public:
  using executor_type = asio::any_io_executor;
  template <typename T>
  using awaitable = asio::awaitable<T, executor_type>;

  explicit async_connection_pool(executor_type executor,
                                 pool_options options = {})
      : executor_(std::move(executor)), strand_(executor_), options_(options) {}

  // Destructor - automatically cleanup connections
  ~async_connection_pool() {
    if (initialized_ || !available_connections_.empty()) {
      if (in_use_count_ > 0 && options_.log_pool_exhaustion) {
        std::cerr << "[Connection Pool] Warning: Pool destroyed with "
                  << in_use_count_ << " connections still in use. "
                  << "Consider calling close_all() before destruction."
                  << std::endl;
      }
    }
    available_connections_.clear();
  }

  // Disable copy and move
  async_connection_pool(const async_connection_pool&) = delete;
  async_connection_pool& operator=(const async_connection_pool&) = delete;
  async_connection_pool(async_connection_pool&&) = delete;
  async_connection_pool& operator=(async_connection_pool&&) = delete;

  // Initialize the connection pool
  awaitable<bool> init(size_t pool_size, const std::string& host,
                       const std::string& user = "",
                       const std::string& passwd = "",
                       const std::string& db = "",
                       const std::optional<int>& timeout = {},
                       const std::optional<int>& port = {}) {
    if (initialized_) {
      co_return true;
    }
    if (closing_) {
      co_return false;
    }

    pool_size_ = pool_size;
    host_ = host;
    user_ = user;
    passwd_ = passwd;
    database_ = db;
    timeout_ = timeout;
    port_ = port;

    // Create initial connections
    for (size_t i = 0; i < pool_size_; ++i) {
      auto conn = std::make_unique<DB>(executor_);

      bool connected = co_await conn->connect(host_, user_, passwd_, database_,
                                              timeout_, port_);
      if (!connected) {
        co_await disconnect_available_connections();
        co_return false;
      }

      available_connections_.push_back(std::move(conn));
    }

    initialized_ = true;
    co_return true;
  }

  // Get a connection from the pool (with timeout)
  // Returns a shared_ptr with custom deleter that automatically returns
  // connection to pool
  awaitable<std::shared_ptr<DB>> get(
      std::chrono::seconds timeout = std::chrono::seconds(10)) {
    auto deadline = std::chrono::steady_clock::now() + timeout;
    size_t wait_count = 0;

    while (std::chrono::steady_clock::now() < deadline) {
      // Try to get an available connection (thread-safe via strand)
      co_await asio::post(strand_, asio::use_awaitable);

      if (!initialized_ || closing_) {
        co_return nullptr;
      }

      if (!available_connections_.empty()) {
        auto connection = std::move(available_connections_.front());
        available_connections_.pop_front();
        in_use_count_++;

        // Verify connection is still alive
        bool alive = co_await connection->ping();
        if (!alive) {
          // Reconnect
          alive = co_await connection->connect(host_, user_, passwd_, database_,
                                               timeout_, port_);
          if (!alive) {
            co_await asio::post(strand_, asio::use_awaitable);
            if (in_use_count_ > 0) {
              --in_use_count_;
            }
            co_await connection->disconnect();
            continue;
          }
        }

        co_await asio::post(strand_, asio::use_awaitable);
        if (!initialized_ || closing_) {
          if (in_use_count_ > 0) {
            --in_use_count_;
          }
          co_await connection->disconnect();
          continue;
        }

        co_return make_connection_handle(std::move(connection), false);
      }

      // Pool is exhausted
      wait_count++;

      // Try dynamic expansion if enabled
      if (options_.enable_dynamic_expansion &&
          dynamic_connection_count_ < options_.max_dynamic_connections) {
        auto temp_conn = co_await create_dynamic_connection();
        if (temp_conn) {
          co_return temp_conn;
        }
      }

      // Log pool exhaustion (only once per get_connection call)
      if (wait_count == 1 && options_.log_pool_exhaustion) {
        log_pool_exhausted();
      }

      // Wait for a connection to be returned
      // Use exponential backoff: 50ms, 100ms, 200ms, max 500ms
      auto wait_time =
          std::min(std::chrono::milliseconds(
                       50 * (1 << std::min(wait_count - 1, size_t(3)))),
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

    // Timeout - log and return nullptr
    if (options_.log_pool_exhaustion) {
      log_connection_timeout(timeout);
    }

    co_return nullptr;
  }

  // Get pool statistics
  // Returns: (pool_size, available, in_use, dynamic_count)
  awaitable<std::tuple<size_t, size_t, size_t, size_t>> get_stats() {
    co_await asio::post(strand_, asio::use_awaitable);
    co_return std::make_tuple(pool_size_, available_connections_.size(),
                              in_use_count_, dynamic_connection_count_);
  }

  // Get detailed pool status
  awaitable<std::string> get_status_string() {
    auto [total, available, in_use, dynamic] = co_await get_stats();

    std::string status = "Connection Pool Status:\n";
    status += "  Pool size: " + std::to_string(total) + "\n";
    status += "  Available: " + std::to_string(available) + "\n";
    status += "  In use: " + std::to_string(in_use) + "\n";
    status += "  Dynamic: " + std::to_string(dynamic) + "\n";
    const auto capacity = total + dynamic;
    status += "  Utilization: " +
              std::to_string(capacity == 0 ? 0 : in_use * 100 / capacity) + "%";

    co_return status;
  }

  // Close all connections gracefully
  // wait_for_return: if true, wait for all in-use connections to be returned
  awaitable<void> close_all(
      bool wait_for_return = false,
      std::chrono::seconds max_wait = std::chrono::seconds(30)) {
    co_await asio::post(strand_, asio::use_awaitable);

    if (!initialized_ || closing_) {
      co_return;
    }

    closing_ = true;

    if (wait_for_return && in_use_count_ > 0) {
      auto deadline = std::chrono::steady_clock::now() + max_wait;

      while (std::chrono::steady_clock::now() < deadline) {
        co_await asio::post(strand_, asio::use_awaitable);

        if (in_use_count_ == 0) {
          break;
        }

        if (options_.log_pool_exhaustion) {
          std::cout << "[Connection Pool] Waiting for " << in_use_count_
                    << " connections to be returned..." << std::endl;
        }

        auto remaining = deadline - std::chrono::steady_clock::now();
        if (remaining <= std::chrono::steady_clock::duration::zero()) {
          break;
        }

        asio::steady_timer timer(executor_);
        auto wait_time = std::min(
            std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                std::chrono::seconds(1)),
            remaining);
        timer.expires_after(wait_time);
        co_await timer.async_wait(asio::use_awaitable);
      }

      if (in_use_count_ > 0) {
        std::cerr
            << "[Connection Pool] Warning: Timeout waiting for connections. "
            << in_use_count_ << " connections still in use." << std::endl;
      }
    }

    co_await asio::post(strand_, asio::use_awaitable);

    // Disconnect all available connections
    for (auto& conn : available_connections_) {
      co_await conn->disconnect();
    }
    available_connections_.clear();

    initialized_ = false;
    closing_ = false;

    if (options_.log_pool_exhaustion) {
      std::cout << "[Connection Pool] Connection pool closed." << std::endl;
    }
  }

 private:
  std::shared_ptr<DB> make_connection_handle(std::unique_ptr<DB> connection,
                                             bool dynamic) {
    auto raw = connection.release();
    auto weak_pool = this->weak_from_this();

    try {
      return std::shared_ptr<DB>(raw, [weak_pool, dynamic](DB* db) mutable {
        std::unique_ptr<DB> connection(db);
        try {
          if (auto pool = weak_pool.lock()) {
            auto strand = pool->strand_;
            asio::post(strand, [pool = std::move(pool),
                                connection = std::move(connection),
                                dynamic]() mutable {
              pool->return_connection(std::move(connection), dynamic);
            });
          }
        } catch (...) {
          // Fallback to local destruction of the connection handle.
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
      return;
    }

    if (in_use_count_ > 0) {
      --in_use_count_;
    }

    if (initialized_ && !closing_) {
      available_connections_.push_back(std::move(connection));
    }
  }

  awaitable<void> disconnect_available_connections() {
    for (auto& conn : available_connections_) {
      co_await conn->disconnect();
    }
    available_connections_.clear();
  }

  // Create a dynamic (temporary) connection
  awaitable<std::shared_ptr<DB>> create_dynamic_connection() {
    co_await asio::post(strand_, asio::use_awaitable);

    if (!initialized_ || closing_ ||
        dynamic_connection_count_ >= options_.max_dynamic_connections) {
      co_return nullptr;
    }

    dynamic_connection_count_++;
    in_use_count_++;

    auto conn = std::make_unique<DB>(executor_);

    bool connected = co_await conn->connect(host_, user_, passwd_, database_,
                                            timeout_, port_);
    if (!connected) {
      co_await asio::post(strand_, asio::use_awaitable);
      dynamic_connection_count_--;
      in_use_count_--;
      co_return nullptr;
    }

    co_await asio::post(strand_, asio::use_awaitable);
    if (!initialized_ || closing_) {
      if (dynamic_connection_count_ > 0) {
        --dynamic_connection_count_;
      }
      if (in_use_count_ > 0) {
        --in_use_count_;
      }
      co_await conn->disconnect();
      co_return nullptr;
    }

    co_return make_connection_handle(std::move(conn), true);
  }

  void log_pool_exhausted() {
    // This would be called from strand context
    std::cerr << "[Connection Pool] Pool exhausted - all " << pool_size_
              << " connections in use. Waiting for available connection..."
              << std::endl;
  }

  void log_connection_timeout(std::chrono::seconds timeout) {
    std::cerr << "[Connection Pool] Timeout after " << timeout.count()
              << " seconds waiting for connection. "
              << "Consider increasing pool size or timeout." << std::endl;
  }

  executor_type executor_;
  asio::strand<executor_type> strand_;
  pool_options options_;

  bool initialized_ = false;
  bool closing_ = false;
  size_t pool_size_ = 0;
  size_t in_use_count_ = 0;
  size_t dynamic_connection_count_ = 0;

  std::string host_;
  std::string user_;
  std::string passwd_;
  std::string database_;
  std::optional<int> timeout_;
  std::optional<int> port_;

  std::deque<std::unique_ptr<DB>> available_connections_;
};

}  // namespace ormpp

#endif  // ORMPP_ASYNC_CONNECTION_POOL_HPP
