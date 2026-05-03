#pragma once

#include <asio/any_io_executor.hpp>
#include <asio/awaitable.hpp>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/post.hpp>
#include <coroutine>
#include <exception>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <variant>

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
      } else {
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
  AsioAwaitableAdapter(
      asio::awaitable<T, asio::any_io_executor> awaitable,
      asio::any_io_executor executor)
      : awaitable_(std::move(awaitable)), asio_executor_(std::move(executor)) {}

  AsioAwaitableAdapter(const AsioAwaitableAdapter&) = delete;
  AsioAwaitableAdapter& operator=(const AsioAwaitableAdapter&) = delete;
  AsioAwaitableAdapter(AsioAwaitableAdapter&&) = default;
  AsioAwaitableAdapter& operator=(AsioAwaitableAdapter&&) = default;

  bool await_ready() const noexcept {
    return false;
  }

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
    } else {
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
inline auto from_asio(
    asio::awaitable<T, asio::any_io_executor> awaitable,
    asio::any_io_executor executor) {
  return AsioAwaitableAdapter<T>(std::move(awaitable), executor);
}

} // namespace adapter
