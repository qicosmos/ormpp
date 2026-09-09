#ifndef ORMPP_ASYNC_CONNECTION_POOL_HPP
#define ORMPP_ASYNC_CONNECTION_POOL_HPP

#include <algorithm>
#include <asio.hpp>
#include <atomic>
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
#include <vector>

#include "async_traits.hpp"

namespace ormpp {

// Pool configuration options
struct pool_options {
  bool enable_dynamic_expansion =
      false;  // Allow temporary connections when pool is full
  size_t max_dynamic_connections = 10;  // Max temporary connections
  bool log_pool_exhaustion = true;      // Log when pool is exhausted

  // Heartbeat: periodically ping idle connections, drop dead ones and
  // rebuild capacity so the pool heals without waiting for a business
  // request.
  bool enable_heartbeat = true;
  std::chrono::milliseconds heartbeat_interval{30000};
  std::chrono::milliseconds ping_timeout{2000};
  std::chrono::milliseconds reconnect_initial_delay{500};
  std::chrono::milliseconds reconnect_max_delay{30000};
  size_t heartbeat_ping_batch =
      8;  // Max concurrent pings per round (0 = unlimited)
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
    initialized_ = false;  // stop the heartbeat loop if it is still running
    if (!available_connections_.empty() || in_use_count_ > 0) {
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

    // The pool must be owned by std::shared_ptr: the heartbeat loop and the
    // connection-return deleter both go through weak_from_this().
    if (this->weak_from_this().expired()) {
      if (options_.log_pool_exhaustion) {
        std::cerr << "[Connection Pool] Error: pool must be owned by "
                     "std::shared_ptr; init() failed because connections "
                     "cannot be returned to the pool without it."
                  << std::endl;
      }
      co_return false;
    }

    // Normalize heartbeat options so misconfigured values cannot cause
    // busy loops or broken backoff.
    bool normalized = false;
    auto clamp_min = [&](auto& value, auto minimum) {
      if (value < minimum) {
        value = minimum;
        normalized = true;
      }
    };
    clamp_min(options_.heartbeat_interval, std::chrono::milliseconds(1));
    clamp_min(options_.ping_timeout, std::chrono::milliseconds(1));
    clamp_min(options_.reconnect_initial_delay, std::chrono::milliseconds(1));
    if (options_.reconnect_max_delay < options_.reconnect_initial_delay) {
      options_.reconnect_max_delay = options_.reconnect_initial_delay;
      normalized = true;
    }
    if (normalized && options_.log_pool_exhaustion) {
      std::cerr << "[Connection Pool] Warning: heartbeat options were "
                   "normalized to valid ranges."
                << std::endl;
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
    if (options_.enable_heartbeat) {
      start_heartbeat(++generation_);
    }
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
                              in_use_count_.load(), dynamic_connection_count_);
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
  void start_heartbeat(uint64_t generation) {
    asio::co_spawn(
        executor_,
        [weak = this->weak_from_this(), generation]() -> awaitable<void> {
          // Never let an exception escape to asio::detached: it would
          // silently kill the maintenance loop with no trace.
          try {
            co_await heartbeat_loop(weak, generation);
          } catch (const std::exception& e) {
            std::cerr << "[Connection Pool] Heartbeat loop stopped by "
                         "exception: "
                      << e.what() << std::endl;
          } catch (...) {
            std::cerr << "[Connection Pool] Heartbeat loop stopped by an "
                         "unknown exception."
                      << std::endl;
          }
        },
        asio::detached);
  }

  // Periodic maintenance: ping idle connections, drop dead ones and rebuild
  // capacity. Only idle connections are touched; in-use connections are left
  // to the business code. While capacity is short the loop retries with
  // exponential backoff so the pool recovers as soon as the database is
  // back, without waiting for a business request.
  //
  // The pool is locked via weak_ptr only while the loop touches it (per
  // slice check and per round), so dropping the last user reference
  // destroys the pool promptly and stops the loop.
  static awaitable<void> heartbeat_loop(
      std::weak_ptr<async_connection_pool> weak, uint64_t generation) {
    auto self = weak.lock();
    if (!self) {
      co_return;
    }
    auto reconnect_delay = self->options_.reconnect_initial_delay;
    std::chrono::milliseconds wait_time = std::max(
        self->options_.heartbeat_interval, std::chrono::milliseconds(1));
    asio::steady_timer wait_timer(self->executor_);
    self.reset();

    for (;;) {
      // Wait in small slices, re-checking liveness each slice so
      // close_all()/destruction is noticed promptly instead of blocking on
      // a full-interval timer. The pool is not held while waiting, so it is
      // destroyed promptly after the last user reference is dropped.
      auto remaining = wait_time;
      while (remaining > std::chrono::milliseconds::zero()) {
        auto slice = std::min(remaining, std::chrono::milliseconds(100));
        wait_timer.expires_after(slice);
        co_await wait_timer.async_wait(asio::use_awaitable);
        remaining -= slice;

        self = weak.lock();
        if (!self) {
          co_return;
        }
        co_await asio::post(self->strand_, asio::use_awaitable);
        if (!self->initialized_ || self->closing_ ||
            self->generation_ != generation) {
          co_return;
        }
        self.reset();
      }

      // Hold the pool through the round.
      self = weak.lock();
      if (!self) {
        co_return;
      }
      co_await asio::post(self->strand_, asio::use_awaitable);
      if (!self->initialized_ || self->closing_ ||
          self->generation_ != generation) {
        co_return;
      }

      // Ping idle connections in batches and return each batch immediately
      // after pinging, so a round keeps all but heartbeat_ping_batch
      // connections available and pings stay bounded. Dead connections are
      // disconnected and destroyed inside ping_connections.
      size_t drained = 0;
      for (;;) {
        size_t batch = self->options_.heartbeat_ping_batch;
        if (batch == 0) {
          batch = self->available_connections_.size();  // 0 = unlimited
        }
        std::vector<std::unique_ptr<DB>> idle;
        idle.reserve(std::min(batch, self->available_connections_.size()));
        while (idle.size() < batch && !self->available_connections_.empty()) {
          idle.push_back(std::move(self->available_connections_.front()));
          self->available_connections_.pop_front();
        }
        if (idle.empty()) {
          break;
        }
        drained += idle.size();

        auto batch_alive = co_await self->ping_connections(std::move(idle));

        co_await asio::post(self->strand_, asio::use_awaitable);
        if (!self->initialized_ || self->closing_ ||
            self->generation_ != generation) {
          co_return;  // alive connections are destroyed here
        }
        for (auto& conn : batch_alive) {
          self->available_connections_.push_back(std::move(conn));
        }

        if (drained >= self->pool_size_) {
          break;  // one round checks each fixed connection at most once
        }
      }

      // Rebuild the capacity lost to dead connections.
      size_t gap = 0;
      if (self->available_connections_.size() + self->in_use_count_ <
          self->pool_size_) {
        gap = self->pool_size_ - self->available_connections_.size() -
              self->in_use_count_;
      }

      if (gap > 0) {
        auto replacements = co_await self->create_replacement_connections(gap);
        if (replacements.size() < gap) {
          auto max_delay = self->options_.reconnect_max_delay;
          reconnect_delay = (reconnect_delay >= max_delay / 2)
                                ? max_delay
                                : reconnect_delay * 2;
        }
        if (!replacements.empty()) {
          co_await asio::post(self->strand_, asio::use_awaitable);
          if (!self->initialized_ || self->closing_ ||
              self->generation_ != generation) {
            for (auto& conn : replacements) {
              co_await conn->disconnect();
            }
            co_return;
          }
          for (auto& conn : replacements) {
            self->available_connections_.push_back(std::move(conn));
          }
        }
      }

      co_await asio::post(self->strand_, asio::use_awaitable);
      if (!self->initialized_ || self->closing_ ||
          self->generation_ != generation) {
        co_return;
      }
      if (self->available_connections_.size() + self->in_use_count_ <
          self->pool_size_) {
        wait_time = std::max(
            std::min(reconnect_delay, self->options_.heartbeat_interval),
            std::chrono::milliseconds(1));
      }
      else {
        reconnect_delay = self->options_.reconnect_initial_delay;
        wait_time = self->options_.heartbeat_interval;
      }

      self.reset();  // allow the pool to be destroyed while waiting
    }
  }

  // Ping a batch of connections concurrently. Takes ownership of the
  // connections and returns the alive ones; dead connections are
  // disconnected and destroyed. Every ping coroutine catches all
  // exceptions, so pending always reaches zero.
  //
  // After ping_timeout the stragglers are cancelled and given a short grace
  // period. A ping that ignores cancellation keeps its connection (the
  // coroutine owns it and destroys it whenever it finishes) and is reported
  // dead, so the pool rebuilds capacity instead of waiting forever.
  awaitable<std::vector<std::unique_ptr<DB>>> ping_connections(
      std::vector<std::unique_ptr<DB>> conns) {
    const size_t n = conns.size();
    if (n == 0) {
      co_return conns;
    }

    // Shared holders: a connection is destroyed by whoever drops the last
    // reference to its slot - the round for completed pings, or the ping
    // coroutine itself for pings that hang past the grace period.
    auto holders =
        std::make_shared<std::vector<std::unique_ptr<DB>>>(std::move(conns));
    auto results = std::make_shared<std::vector<std::optional<bool>>>(n);
    auto pending = std::make_shared<std::atomic_size_t>(n);
    asio::cancellation_signal signal;

    for (size_t i = 0; i < n; ++i) {
      asio::co_spawn(
          executor_,
          [holder = holders, results, pending, i]() -> awaitable<void> {
            bool ok = false;
            try {
              ok = co_await (*holder)[i]->ping();
            } catch (...) {
              ok = false;
            }
            (*results)[i] = ok;
            pending->fetch_sub(1, std::memory_order_release);
          },
          asio::bind_cancellation_slot(signal.slot(), asio::detached));
    }

    asio::steady_timer poll_timer(executor_);
    auto deadline = std::chrono::steady_clock::now() + options_.ping_timeout;
    while (pending->load(std::memory_order_acquire) > 0 &&
           std::chrono::steady_clock::now() < deadline) {
      poll_timer.expires_after(std::chrono::milliseconds(10));
      co_await poll_timer.async_wait(asio::use_awaitable);
    }

    if (pending->load(std::memory_order_acquire) > 0) {
      signal.emit(asio::cancellation_type::all);
      auto grace =
          std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
      while (pending->load(std::memory_order_acquire) > 0 &&
             std::chrono::steady_clock::now() < grace) {
        poll_timer.expires_after(std::chrono::milliseconds(1));
        co_await poll_timer.async_wait(asio::use_awaitable);
      }
    }

    // Reclaim connections whose ping completed; a slot with no result
    // belongs to a still-running ping coroutine and must be left alone.
    std::vector<std::unique_ptr<DB>> alive_conns;
    alive_conns.reserve(n);
    for (size_t i = 0; i < n; ++i) {
      if (!(*results)[i].has_value()) {
        continue;
      }
      if ((*results)[i].value()) {
        alive_conns.push_back(std::move((*holders)[i]));
      }
      else {
        (*holders)[i].reset();  // dead: disconnect and destroy
      }
    }
    co_return alive_conns;
  }

  // Create up to `count` connections sequentially; stop at the first
  // failure, since the database is likely down.
  awaitable<std::vector<std::unique_ptr<DB>>> create_replacement_connections(
      size_t count) {
    std::vector<std::unique_ptr<DB>> conns;
    conns.reserve(count);
    for (size_t i = 0; i < count; ++i) {
      auto conn = std::make_unique<DB>(executor_);
      bool connected = co_await conn->connect(host_, user_, passwd_, database_,
                                              timeout_, port_);
      if (!connected) {
        co_await conn->disconnect();
        break;
      }
      conns.push_back(std::move(conn));
    }
    co_return conns;
  }

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

  // Flag/counter state is atomic because the destructor may write it from
  // any thread while the executor thread runs the heartbeat loop.
  std::atomic_bool initialized_ = false;
  std::atomic_bool closing_ = false;
  std::atomic_size_t in_use_count_ = 0;
  std::atomic_uint64_t generation_ = 0;  // bumped on init to invalidate
                                         // old heartbeats
  size_t pool_size_ = 0;
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
