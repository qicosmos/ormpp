#pragma once

#include <asio/any_io_executor.hpp>
#include <asio/awaitable.hpp>
#include <asio/bind_executor.hpp>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/post.hpp>
#include <asio/strand.hpp>
#include <atomic>
#include <coroutine>
#include <exception>
#include <memory>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <variant>

#ifndef ORMPP_ASIO_ASYNC_SIMPLE_ADAPTER_HAS_CANCELLATION
#if defined(__has_include)
#if __has_include(<asio/bind_cancellation_slot.hpp>) && \
    __has_include(<asio/cancellation_signal.hpp>) && \
    __has_include(<asio/cancellation_type.hpp>)
#define ORMPP_ASIO_ASYNC_SIMPLE_ADAPTER_HAS_CANCELLATION 1
#endif
#endif
#endif

#ifndef ORMPP_ASIO_ASYNC_SIMPLE_ADAPTER_HAS_CANCELLATION
#define ORMPP_ASIO_ASYNC_SIMPLE_ADAPTER_HAS_CANCELLATION 0
#endif

#if ORMPP_ASIO_ASYNC_SIMPLE_ADAPTER_HAS_CANCELLATION
#include <asio/bind_cancellation_slot.hpp>
#include <asio/cancellation_signal.hpp>
#include <asio/cancellation_type.hpp>
#endif

namespace async_simple {
class Executor;
}  // namespace async_simple

namespace adapter {

// Adapter: bridges asio::awaitable to async_simple::coro::Lazy
// Allows async_simple coroutines to co_await asio operations
// Both frameworks share the same io_context for zero-copy integration

template <typename T>
class AsioAwaitableAdapter {
 private:
  static_assert(!std::is_reference_v<T>,
                "AsioAwaitableAdapter does not support reference results");

  using stored_type = std::conditional_t<std::is_void_v<T>, std::monostate, T>;

  asio::awaitable<T, asio::any_io_executor> awaitable_;
  asio::any_io_executor asio_executor_;
  std::optional<stored_type> result_;
  std::exception_ptr exception_;
  std::coroutine_handle<> resume_handle_;

  void post_resume() noexcept {
    auto handle = resume_handle_;
    try {
      // Must use post (not dispatch) to avoid lifetime issues:
      // dispatch could inline resume(), destroying awaiter while still in use
      asio::post(asio_executor_, [handle]() mutable {
        handle.resume();
      });
    } catch (...) {
      // Fallback: resume directly if post fails (rare)
      // This bypasses executor but ensures progress in error case
      if (!exception_) {
        exception_ = std::current_exception();
      }
      handle.resume();
    }
  }

  asio::awaitable<void, asio::any_io_executor> run_asio() {
    try {
      if constexpr (std::is_void_v<T>) {
        co_await std::move(awaitable_);
        result_.emplace();
      }
      else {
        result_.emplace(co_await std::move(awaitable_));
      }
    } catch (...) {
      exception_ = std::current_exception();
    }

    post_resume();
  }

  void start_asio() noexcept {
    try {
      asio::co_spawn(asio_executor_, run_asio(), asio::detached);
    } catch (...) {
      exception_ = std::current_exception();
      post_resume();
    }
  }

 public:
  AsioAwaitableAdapter(asio::awaitable<T, asio::any_io_executor> awaitable,
                       asio::any_io_executor executor)
      : awaitable_(std::move(awaitable)), asio_executor_(std::move(executor)) {}

  AsioAwaitableAdapter(const AsioAwaitableAdapter&) = delete;
  AsioAwaitableAdapter& operator=(const AsioAwaitableAdapter&) = delete;
  AsioAwaitableAdapter(AsioAwaitableAdapter&&) = default;
  AsioAwaitableAdapter& operator=(AsioAwaitableAdapter&&) = default;

  bool await_ready() const noexcept { return false; }

  bool await_suspend(std::coroutine_handle<> handle) noexcept {
    resume_handle_ = handle;
    try {
      asio::post(asio_executor_, [this]() {
        start_asio();
      });
    } catch (...) {
      exception_ = std::current_exception();
      return false;  // Resume immediately with exception
    }
    return true;  // Suspend until asio operation completes
  }

  T await_resume() {
    if (exception_) {
      std::rethrow_exception(exception_);
    }
    if constexpr (std::is_void_v<T>) {
      return;
    }
    else {
      if (!result_.has_value()) {
        throw std::runtime_error(
            "asio_async_simple_adapter: missing asio result");
      }
      return std::move(*result_);
    }
  }

  // async_simple 通过 coAwait 识别自定义 awaiter。
  auto coAwait([[maybe_unused]] async_simple::Executor* executor) noexcept {
    return std::move(*this);
  }
};

template <typename T>
inline auto from_asio(asio::awaitable<T, asio::any_io_executor> awaitable,
                      asio::any_io_executor executor) {
  return AsioAwaitableAdapter<T>(std::move(awaitable), executor);
}

// Safe adapter variant for wider execution models.
//
// Compared with AsioAwaitableAdapter, this version keeps all shared state in a
// shared_ptr, serializes Asio-side state changes through a strand, and prevents
// resuming a destroyed parent coroutine when the awaiter is abandoned.
template <typename T>
class SafeAsioAwaitableAdapter {
 private:
  static_assert(!std::is_reference_v<T>,
                "SafeAsioAwaitableAdapter does not support reference results");

  using executor_type = asio::any_io_executor;
  using strand_type = asio::strand<executor_type>;
  using stored_type = std::conditional_t<std::is_void_v<T>, std::monostate, T>;

  struct SharedState {
    SharedState(asio::awaitable<T, executor_type> awaitable,
                executor_type executor)
        : awaitable(std::move(awaitable)),
          executor(std::move(executor)),
          strand(this->executor) {}

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
    asio::cancellation_type pending_cancellation =
        asio::cancellation_type::none;
#endif
  };

  std::shared_ptr<SharedState> state_;

  static bool should_resume(
      const std::shared_ptr<SharedState>& state) noexcept {
    return state && !state->abandoned.load(std::memory_order_acquire) &&
           !state->resume_observed.load(std::memory_order_acquire) &&
           state->resume_handle;
  }

  static asio::awaitable<void, executor_type> run_asio(
      std::shared_ptr<SharedState> state) {
    try {
      if constexpr (std::is_void_v<T>) {
        co_await std::move(state->awaitable);
        state->result.emplace();
      }
      else {
        state->result.emplace(co_await std::move(state->awaitable));
      }
    } catch (...) {
      state->exception = std::current_exception();
    }

    state->finished = true;
    co_return;
  }

  static void post_resume(std::shared_ptr<SharedState> state) noexcept {
    try {
      auto strand = state->strand;
      asio::post(strand, [state = std::move(state)]() mutable {
        resume_parent(std::move(state));
      });
    } catch (...) {
      if (!state->exception) {
        state->exception = std::current_exception();
      }

      resume_parent(std::move(state));
    }
  }

  static void resume_parent(std::shared_ptr<SharedState> state) noexcept {
    if (!should_resume(state)) {
      return;
    }

    auto handle = state->resume_handle;
    auto* async_executor =
        state->async_executor.load(std::memory_order_acquire);
    if (async_executor) {
      try {
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

    handle.resume();
  }

  static void complete_asio(std::shared_ptr<SharedState> state,
                            std::exception_ptr exception) noexcept {
    try {
      if (exception && !state->exception) {
        state->exception = exception;
      }
      if (!state->finished && !state->exception) {
        state->exception = std::make_exception_ptr(std::runtime_error(
            "asio_async_simple_adapter: safe asio coroutine finished without "
            "a result"));
      }
    } catch (...) {
      if (!state->exception) {
        state->exception = std::current_exception();
      }
    }

    post_resume(std::move(state));
  }

  static void start_asio(std::shared_ptr<SharedState> state) noexcept {
    if (state->abandoned.load(std::memory_order_acquire)) {
      return;
    }

    try {
      auto strand = state->strand;
      auto run_state = state;
      auto completion_state = state;
#if ORMPP_ASIO_ASYNC_SIMPLE_ADAPTER_HAS_CANCELLATION
      auto cancel_slot = completion_state->cancellation.slot();
      auto completion_handler = asio::bind_executor(
          strand, [state = std::move(completion_state)](
                      std::exception_ptr exception) mutable {
            complete_asio(std::move(state), exception);
          });
      auto completion_token = asio::bind_cancellation_slot(
          cancel_slot, std::move(completion_handler));
      asio::co_spawn(strand, run_asio(std::move(run_state)),
                     std::move(completion_token));
      if (state->pending_cancellation != asio::cancellation_type::none) {
        state->cancellation.emit(state->pending_cancellation);
      }
#else
      auto completion_handler = asio::bind_executor(
          strand, [state = std::move(completion_state)](
                      std::exception_ptr exception) mutable {
            complete_asio(std::move(state), exception);
          });
      asio::co_spawn(strand, run_asio(std::move(run_state)),
                     std::move(completion_handler));
#endif
    } catch (...) {
      state->exception = std::current_exception();
      state->finished = true;
      post_resume(std::move(state));
    }
  }

#if ORMPP_ASIO_ASYNC_SIMPLE_ADAPTER_HAS_CANCELLATION
  static bool request_cancel(const std::shared_ptr<SharedState>& state,
                             asio::cancellation_type type) noexcept {
    if (!state) {
      return false;
    }
    if (state->resume_observed.load(std::memory_order_acquire)) {
      return false;
    }

    try {
      asio::post(state->strand, [state, type]() mutable {
        if (state->finished ||
            state->resume_observed.load(std::memory_order_acquire)) {
          return;
        }
        state->pending_cancellation |= type;
        state->cancellation.emit(type);
      });
      return true;
    } catch (...) {
      return false;
    }
  }
#else
  static bool request_cancel(const std::shared_ptr<SharedState>&) noexcept {
    return false;
  }
#endif

  static void abandon(std::shared_ptr<SharedState> state) noexcept {
    if (!state || state->resume_observed.load(std::memory_order_acquire)) {
      return;
    }

    state->abandoned.store(true, std::memory_order_release);
    if (!state->await_started.load(std::memory_order_acquire)) {
      return;
    }
#if ORMPP_ASIO_ASYNC_SIMPLE_ADAPTER_HAS_CANCELLATION
    request_cancel(state, asio::cancellation_type::all);
#else
    request_cancel(state);
#endif
  }

 public:
  class cancellation_handle {
   public:
    cancellation_handle() = default;

    bool valid() const noexcept { return !state_.expired(); }

#if ORMPP_ASIO_ASYNC_SIMPLE_ADAPTER_HAS_CANCELLATION
    bool cancel(asio::cancellation_type type =
                    asio::cancellation_type::all) const noexcept {
      return request_cancel(state_.lock(), type);
    }
#else
    bool cancel() const noexcept { return request_cancel(state_.lock()); }
#endif

   private:
    friend class SafeAsioAwaitableAdapter;
    explicit cancellation_handle(std::weak_ptr<SharedState> state)
        : state_(std::move(state)) {}

    std::weak_ptr<SharedState> state_;
  };

  SafeAsioAwaitableAdapter(asio::awaitable<T, executor_type> awaitable,
                           executor_type executor)
      : state_(std::make_shared<SharedState>(std::move(awaitable),
                                             std::move(executor))) {}

  ~SafeAsioAwaitableAdapter() { abandon(std::move(state_)); }

  SafeAsioAwaitableAdapter(const SafeAsioAwaitableAdapter&) = delete;
  SafeAsioAwaitableAdapter& operator=(const SafeAsioAwaitableAdapter&) = delete;

  SafeAsioAwaitableAdapter(SafeAsioAwaitableAdapter&& other) noexcept
      : state_(std::exchange(other.state_, nullptr)) {}
  SafeAsioAwaitableAdapter& operator=(
      SafeAsioAwaitableAdapter&& other) noexcept {
    if (this != &other) {
      abandon(std::move(state_));
      state_ = std::exchange(other.state_, nullptr);
    }
    return *this;
  }

  bool await_ready() const noexcept { return false; }

  bool await_suspend(std::coroutine_handle<> handle) noexcept {
    if (!state_) {
      return false;
    }

    state_->resume_handle = handle;
    state_->await_started.store(true, std::memory_order_release);

    try {
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

  T await_resume() {
    if (!state_) {
      throw std::runtime_error(
          "asio_async_simple_adapter: safe awaiter has no state");
    }

    state_->resume_observed.store(true, std::memory_order_release);

    if (state_->exception) {
      std::rethrow_exception(state_->exception);
    }
    if constexpr (std::is_void_v<T>) {
      return;
    }
    else {
      if (!state_->result.has_value()) {
        throw std::runtime_error(
            "asio_async_simple_adapter: missing safe asio result");
      }

      auto result = std::move(*state_->result);
      state_->result.reset();
      return result;
    }
  }

  cancellation_handle get_cancellation_handle() const noexcept {
    return cancellation_handle{state_};
  }

#if ORMPP_ASIO_ASYNC_SIMPLE_ADAPTER_HAS_CANCELLATION
  bool cancel(asio::cancellation_type type =
                  asio::cancellation_type::all) const noexcept {
    return request_cancel(state_, type);
  }

  static constexpr bool cancellation_supported() noexcept { return true; }
#else
  bool cancel() const noexcept { return request_cancel(state_); }

  static constexpr bool cancellation_supported() noexcept { return false; }
#endif

  auto coAwait([[maybe_unused]] async_simple::Executor* executor) noexcept {
    if (state_) {
      state_->async_executor.store(executor, std::memory_order_release);
    }
    return std::move(*this);
  }
};

template <typename T>
inline auto from_asio_safe(asio::awaitable<T, asio::any_io_executor> awaitable,
                           asio::any_io_executor executor) {
  return SafeAsioAwaitableAdapter<T>(std::move(awaitable), std::move(executor));
}

}  // namespace adapter
