#ifdef ORMPP_ENABLE_MYSQL_ASYNC

#include <algorithm>
#include <array>
#include <asio.hpp>
#include <chrono>
#include <coroutine>
#include <exception>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "doctest.h"
#include "ormpp/asio_async_simple_adapter.hpp"

namespace {

template <typename T>
class TestTask {
 public:
  struct promise_type {
    std::optional<T> value;
    std::exception_ptr exception;
    bool* done = nullptr;

    TestTask get_return_object() {
      return TestTask(std::coroutine_handle<promise_type>::from_promise(*this));
    }

    std::suspend_always initial_suspend() noexcept { return {}; }

    struct FinalAwaiter {
      bool await_ready() noexcept { return false; }
      void await_suspend(std::coroutine_handle<promise_type> handle) noexcept {
        if (auto* done = handle.promise().done) {
          *done = true;
        }
      }
      void await_resume() noexcept {}
    };

    FinalAwaiter final_suspend() noexcept { return {}; }

    template <typename U>
    void return_value(U&& result) {
      value.emplace(std::forward<U>(result));
    }

    void unhandled_exception() noexcept {
      exception = std::current_exception();
    }
  };

  explicit TestTask(std::coroutine_handle<promise_type> handle)
      : handle_(handle) {}
  TestTask(TestTask&& other) noexcept
      : handle_(std::exchange(other.handle_, nullptr)) {}
  TestTask& operator=(TestTask&& other) noexcept {
    if (this != &other) {
      destroy();
      handle_ = std::exchange(other.handle_, nullptr);
    }
    return *this;
  }
  TestTask(const TestTask&) = delete;
  TestTask& operator=(const TestTask&) = delete;
  ~TestTask() { destroy(); }

  void start(bool& done) {
    done = false;
    handle_.promise().done = &done;
    handle_.resume();
  }

  T result() {
    auto& promise = handle_.promise();
    if (promise.exception) {
      std::rethrow_exception(promise.exception);
    }
    REQUIRE(promise.value.has_value());
    return std::move(*promise.value);
  }

 private:
  void destroy() noexcept {
    if (handle_) {
      handle_.destroy();
      handle_ = nullptr;
    }
  }

  std::coroutine_handle<promise_type> handle_;
};

template <>
class TestTask<void> {
 public:
  struct promise_type {
    std::exception_ptr exception;
    bool* done = nullptr;

    TestTask get_return_object() {
      return TestTask(std::coroutine_handle<promise_type>::from_promise(*this));
    }

    std::suspend_always initial_suspend() noexcept { return {}; }

    struct FinalAwaiter {
      bool await_ready() noexcept { return false; }
      void await_suspend(std::coroutine_handle<promise_type> handle) noexcept {
        if (auto* done = handle.promise().done) {
          *done = true;
        }
      }
      void await_resume() noexcept {}
    };

    FinalAwaiter final_suspend() noexcept { return {}; }
    void return_void() noexcept {}
    void unhandled_exception() noexcept {
      exception = std::current_exception();
    }
  };

  explicit TestTask(std::coroutine_handle<promise_type> handle)
      : handle_(handle) {}
  TestTask(TestTask&& other) noexcept
      : handle_(std::exchange(other.handle_, nullptr)) {}
  TestTask& operator=(TestTask&& other) noexcept {
    if (this != &other) {
      destroy();
      handle_ = std::exchange(other.handle_, nullptr);
    }
    return *this;
  }
  TestTask(const TestTask&) = delete;
  TestTask& operator=(const TestTask&) = delete;
  ~TestTask() { destroy(); }

  void start(bool& done) {
    done = false;
    handle_.promise().done = &done;
    handle_.resume();
  }

  void result() {
    if (handle_.promise().exception) {
      std::rethrow_exception(handle_.promise().exception);
    }
  }

 private:
  void destroy() noexcept {
    if (handle_) {
      handle_.destroy();
      handle_ = nullptr;
    }
  }

  std::coroutine_handle<promise_type> handle_;
};

void run_until(asio::io_context& ctx, const std::function<bool()>& done) {
  for (std::size_t steps = 0; !done(); ++steps) {
    ctx.restart();
    auto count = ctx.run_one();
    REQUIRE_MESSAGE(count == 1, "io_context stopped before task completion");
    REQUIRE_MESSAGE(steps < 100000, "too many io_context steps");
  }
}

std::size_t index_of(const std::vector<std::string>& events,
                     const std::string& event) {
  auto it = std::find(events.begin(), events.end(), event);
  REQUIRE(it != events.end());
  return static_cast<std::size_t>(std::distance(events.begin(), it));
}

asio::awaitable<int, asio::any_io_executor> immediate_value(int value) {
  co_return value;
}

asio::awaitable<int, asio::any_io_executor> posted_value(
    asio::any_io_executor executor, int value) {
  co_await asio::post(executor, asio::use_awaitable);
  co_return value;
}

asio::awaitable<int, asio::any_io_executor> timer_value(
    asio::any_io_executor executor, int value) {
  asio::steady_timer timer(executor);
  timer.expires_after(std::chrono::milliseconds(0));
  co_await timer.async_wait(asio::use_awaitable);
  co_return value;
}

asio::awaitable<void, asio::any_io_executor> immediate_void() { co_return; }

asio::awaitable<void, asio::any_io_executor> posted_void(
    asio::any_io_executor executor) {
  co_await asio::post(executor, asio::use_awaitable);
}

asio::awaitable<int, asio::any_io_executor> throwing_value(
    asio::any_io_executor executor, bool defer) {
  if (defer) {
    co_await asio::post(executor, asio::use_awaitable);
  }
  throw std::runtime_error(defer ? "deferred boom" : "immediate boom");
}

asio::awaitable<int, asio::any_io_executor> start_probe_value(
    std::vector<std::string>* events, int value) {
  events->push_back("child-start");
  co_return value;
}

asio::awaitable<int, asio::any_io_executor> resume_probe_value(
    asio::any_io_executor executor, std::vector<std::string>* events,
    int value) {
  events->push_back("child-ready");
  asio::post(executor, [events] {
    events->push_back("sentinel-before-resume");
  });
  co_return value;
}

TestTask<int> start_is_deferred_task(asio::any_io_executor executor,
                                     std::vector<std::string>* events) {
  events->push_back("task-before-await");
  auto value =
      co_await adapter::from_asio(start_probe_value(events, 7), executor)
          .coAwait(nullptr);
  events->push_back("task-after-await");
  co_return value;
}

TestTask<int> resume_is_deferred_task(asio::any_io_executor executor,
                                      std::vector<std::string>* events) {
  events->push_back("task-before-await");
  auto value = co_await adapter::from_asio(
                   resume_probe_value(executor, events, 11), executor)
                   .coAwait(nullptr);
  events->push_back("task-after-await");
  co_return value;
}

TestTask<void> value_void_and_exception_task(asio::any_io_executor executor) {
  auto a = co_await adapter::from_asio(immediate_value(17), executor)
               .coAwait(nullptr);
  CHECK(a == 17);

  auto b = co_await adapter::from_asio(posted_value(executor, 19), executor)
               .coAwait(nullptr);
  CHECK(b == 19);

  auto c = co_await adapter::from_asio(timer_value(executor, 23), executor)
               .coAwait(nullptr);
  CHECK(c == 23);

  co_await adapter::from_asio(immediate_void(), executor).coAwait(nullptr);
  co_await adapter::from_asio(posted_void(executor), executor).coAwait(nullptr);

  int exceptions = 0;
  try {
    (void)(co_await adapter::from_asio(throwing_value(executor, false),
                                       executor)
               .coAwait(nullptr));
  } catch (const std::runtime_error& e) {
    CHECK(std::string(e.what()) == "immediate boom");
    ++exceptions;
  }

  try {
    (void)(co_await adapter::from_asio(throwing_value(executor, true), executor)
               .coAwait(nullptr));
  } catch (const std::runtime_error& e) {
    CHECK(std::string(e.what()) == "deferred boom");
    ++exceptions;
  }

  CHECK(exceptions == 2);
}

asio::awaitable<int, asio::any_io_executor> patterned_value(
    asio::any_io_executor executor, int value, int mode) {
  switch (mode % 3) {
    case 0:
      co_return value;
    case 1:
      co_return co_await posted_value(executor, value);
    default:
      co_return co_await timer_value(executor, value);
  }
}

TestTask<int> stress_task(asio::any_io_executor executor, int task_index,
                          int await_count, int* resume_count) {
  int sum = 0;
  for (int i = 0; i < await_count; ++i) {
    auto value = task_index * 1000 + i;
    if ((task_index + i) % 11 == 0) {
      try {
        (void)(co_await adapter::from_asio(throwing_value(executor, i % 2 == 0),
                                           executor)
                   .coAwait(nullptr));
      } catch (const std::runtime_error&) {
        ++(*resume_count);
        sum -= value;
      }
      continue;
    }

    sum += co_await adapter::from_asio(
               patterned_value(executor, value, task_index + i), executor)
               .coAwait(nullptr);
    ++(*resume_count);
  }
  co_return sum;
}

int expected_stress_sum(int task_index, int await_count) {
  int sum = 0;
  for (int i = 0; i < await_count; ++i) {
    auto value = task_index * 1000 + i;
    if ((task_index + i) % 11 == 0) {
      sum -= value;
    }
    else {
      sum += value;
    }
  }
  return sum;
}

}  // namespace

TEST_CASE("asio async_simple adapter timing: start and resume are posted") {
  asio::io_context ctx;
  auto executor = ctx.get_executor();

  SUBCASE("await_suspend does not start asio inline") {
    std::vector<std::string> events;
    bool done = false;
    auto task = start_is_deferred_task(executor, &events);

    asio::post(ctx, [&] {
      events.push_back("handler-enter");
      task.start(done);
      events.push_back("handler-exit");
    });

    ctx.restart();
    REQUIRE(ctx.run_one() == 1);
    CHECK(events == std::vector<std::string>{
                        "handler-enter", "task-before-await", "handler-exit"});

    run_until(ctx, [&] {
      return done;
    });
    CHECK(task.result() == 7);
    CHECK(index_of(events, "handler-exit") < index_of(events, "child-start"));
    CHECK(index_of(events, "child-start") <
          index_of(events, "task-after-await"));
  }

  SUBCASE("completion does not resume parent inline") {
    std::vector<std::string> events;
    bool done = false;
    auto task = resume_is_deferred_task(executor, &events);

    asio::post(ctx, [&] {
      task.start(done);
    });
    run_until(ctx, [&] {
      return done;
    });

    CHECK(task.result() == 11);
    CHECK(index_of(events, "child-ready") <
          index_of(events, "sentinel-before-resume"));
    CHECK(index_of(events, "sentinel-before-resume") <
          index_of(events, "task-after-await"));
  }
}

TEST_CASE("asio async_simple adapter timing: result and exception paths") {
  asio::io_context ctx;
  bool done = false;
  auto task = value_void_and_exception_task(ctx.get_executor());

  asio::post(ctx, [&] {
    task.start(done);
  });
  run_until(ctx, [&] {
    return done;
  });
  CHECK_NOTHROW(task.result());
}

TEST_CASE("asio async_simple adapter timing: interleaved stress") {
  constexpr int task_count = 64;
  constexpr int await_count = 16;

  asio::io_context ctx;
  auto executor = ctx.get_executor();
  std::vector<TestTask<int>> tasks;
  std::array<bool, task_count> done{};
  std::vector<int> resume_counts(task_count, 0);
  tasks.reserve(task_count);

  for (int i = 0; i < task_count; ++i) {
    tasks.push_back(stress_task(executor, i, await_count, &resume_counts[i]));
  }

  for (int i = 0; i < task_count; ++i) {
    asio::post(ctx, [&, i] {
      tasks[i].start(done[i]);
    });
  }

  run_until(ctx, [&] {
    return std::all_of(done.begin(), done.end(), [](bool item) {
      return item;
    });
  });

  for (int i = 0; i < task_count; ++i) {
    CHECK(resume_counts[i] == await_count);
    CHECK(tasks[i].result() == expected_stress_sum(i, await_count));
  }
}

#endif  // ORMPP_ENABLE_MYSQL_ASYNC
