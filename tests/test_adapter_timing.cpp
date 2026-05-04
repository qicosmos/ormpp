#ifdef ORMPP_ENABLE_MYSQL_ASYNC

#include <algorithm>
#include <array>
#include <asio.hpp>
#include <atomic>
#include <chrono>
#include <coroutine>
#include <exception>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>
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

class ConcurrentTask {
 public:
  struct promise_type {
    ConcurrentTask get_return_object() {
      return ConcurrentTask(
          std::coroutine_handle<promise_type>::from_promise(*this));
    }

    std::suspend_always initial_suspend() noexcept { return {}; }
    std::suspend_always final_suspend() noexcept { return {}; }
    void return_void() noexcept {}
    void unhandled_exception() noexcept { std::terminate(); }
  };

  explicit ConcurrentTask(std::coroutine_handle<promise_type> handle)
      : handle_(handle) {}
  ConcurrentTask(ConcurrentTask&& other) noexcept
      : handle_(std::exchange(other.handle_, nullptr)) {}
  ConcurrentTask& operator=(ConcurrentTask&& other) noexcept {
    if (this != &other) {
      destroy();
      handle_ = std::exchange(other.handle_, nullptr);
    }
    return *this;
  }
  ConcurrentTask(const ConcurrentTask&) = delete;
  ConcurrentTask& operator=(const ConcurrentTask&) = delete;
  ~ConcurrentTask() { destroy(); }

  void start() { handle_.resume(); }

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

TestTask<void> safe_value_void_and_exception_task(
    asio::any_io_executor executor) {
  auto a = co_await adapter::from_asio_safe(immediate_value(17), executor)
               .coAwait(nullptr);
  CHECK(a == 17);

  auto b =
      co_await adapter::from_asio_safe(posted_value(executor, 19), executor)
          .coAwait(nullptr);
  CHECK(b == 19);

  co_await adapter::from_asio_safe(immediate_void(), executor).coAwait(nullptr);
  co_await adapter::from_asio_safe(posted_void(executor), executor)
      .coAwait(nullptr);

  int exceptions = 0;
  try {
    (void)(co_await adapter::from_asio_safe(throwing_value(executor, false),
                                            executor)
               .coAwait(nullptr));
  } catch (const std::runtime_error& e) {
    CHECK(std::string(e.what()) == "immediate boom");
    ++exceptions;
  }

  try {
    (void)(co_await adapter::from_asio_safe(throwing_value(executor, true),
                                            executor)
               .coAwait(nullptr));
  } catch (const std::runtime_error& e) {
    CHECK(std::string(e.what()) == "deferred boom");
    ++exceptions;
  }

  CHECK(exceptions == 2);
}

asio::awaitable<void, asio::any_io_executor> long_timer(
    asio::any_io_executor executor) {
  asio::steady_timer timer(executor);
  timer.expires_after(std::chrono::hours(1));
  co_await timer.async_wait(asio::use_awaitable);
}

asio::awaitable<void, asio::any_io_executor> observed_timer(
    asio::any_io_executor executor, bool* started, bool* finished) {
  *started = true;

  asio::steady_timer timer(executor);
  timer.expires_after(std::chrono::milliseconds(100));
  try {
    co_await timer.async_wait(asio::use_awaitable);
  } catch (const std::system_error&) {
  }

  *finished = true;
}

TestTask<void> safe_parent_destroyed_task(asio::any_io_executor executor,
                                          bool* child_started,
                                          bool* child_finished,
                                          bool* after_await) {
  co_await adapter::from_asio_safe(
      observed_timer(executor, child_started, child_finished), executor)
      .coAwait(nullptr);
  *after_await = true;
}

ConcurrentTask safe_concurrent_task(asio::any_io_executor executor, int value,
                                    std::vector<int>* results,
                                    std::atomic<int>* completed,
                                    std::atomic<int>* resume_count) {
  auto result = co_await adapter::from_asio_safe(
                    patterned_value(executor, value, value), executor)
                    .coAwait(nullptr);
  (*results)[value] = result;
  resume_count->fetch_add(1, std::memory_order_relaxed);
  completed->fetch_add(1, std::memory_order_release);
}

#if ORMPP_ASIO_ASYNC_SIMPLE_ADAPTER_HAS_CANCELLATION
TestTask<void> safe_cancellation_task(asio::any_io_executor executor,
                                      std::function<bool()>* cancel,
                                      bool* saw_cancel) {
  auto awaiter = adapter::from_asio_safe(long_timer(executor), executor);
  auto cancel_handle = awaiter.get_cancellation_handle();
  *cancel = [cancel_handle]() mutable {
    return cancel_handle.cancel(asio::cancellation_type::terminal);
  };

  try {
    co_await std::move(awaiter).coAwait(nullptr);
  } catch (const std::system_error& e) {
    CHECK(e.code() == asio::error::operation_aborted);
    *saw_cancel = true;
  }
}

TestTask<void> safe_cancel_before_start_task(asio::any_io_executor executor,
                                             bool* saw_cancel) {
  auto awaiter = adapter::from_asio_safe(long_timer(executor), executor);
  CHECK(awaiter.cancel(asio::cancellation_type::terminal));

  try {
    co_await std::move(awaiter).coAwait(nullptr);
  } catch (const std::system_error& e) {
    CHECK(e.code() == asio::error::operation_aborted);
    *saw_cancel = true;
  }
}

TestTask<void> safe_cancel_after_completion_task(
    asio::any_io_executor executor) {
  auto awaiter = adapter::from_asio_safe(immediate_value(42), executor);
  auto cancel_handle = awaiter.get_cancellation_handle();

  auto value = co_await std::move(awaiter).coAwait(nullptr);
  CHECK(value == 42);
  (void)cancel_handle.cancel(asio::cancellation_type::terminal);
}
#endif

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

TEST_CASE("asio async_simple safe adapter: result and exception paths") {
  asio::io_context ctx;
  bool done = false;
  auto task = safe_value_void_and_exception_task(ctx.get_executor());

  asio::post(ctx, [&] {
    task.start(done);
  });
  run_until(ctx, [&] {
    return done;
  });
  CHECK_NOTHROW(task.result());
}

TEST_CASE("asio async_simple safe adapter: parent coroutine destroyed") {
  asio::io_context ctx;
  bool done = false;
  bool child_started = false;
  bool child_finished = false;
  bool after_await = false;

  {
    auto task = safe_parent_destroyed_task(ctx.get_executor(), &child_started,
                                           &child_finished, &after_await);
    asio::post(ctx, [&] {
      task.start(done);
    });

    run_until(ctx, [&] {
      return child_started;
    });

    CHECK_FALSE(done);
    CHECK_FALSE(after_await);
  }

  ctx.restart();
  ctx.run_for(std::chrono::milliseconds(500));

  CHECK(child_finished);
  CHECK_FALSE(done);
  CHECK_FALSE(after_await);
}

TEST_CASE("asio async_simple safe adapter: move semantics") {
  asio::io_context ctx;
  using awaiter_type =
      decltype(adapter::from_asio_safe(immediate_value(1), ctx.get_executor()));
  typename awaiter_type::cancellation_handle handle;

  {
    auto awaiter1 =
        adapter::from_asio_safe(immediate_value(42), ctx.get_executor());
    handle = awaiter1.get_cancellation_handle();
    CHECK(handle.valid());

    auto awaiter2 = std::move(awaiter1);
    CHECK(handle.valid());

    auto awaiter3 =
        adapter::from_asio_safe(immediate_value(99), ctx.get_executor());
    awaiter3 = std::move(awaiter2);
    CHECK(handle.valid());
  }

  CHECK_FALSE(handle.valid());
}

TEST_CASE("asio async_simple safe adapter: cancellation handle lifetime") {
  asio::io_context ctx;
  using awaiter_type =
      decltype(adapter::from_asio_safe(immediate_value(1), ctx.get_executor()));
  typename awaiter_type::cancellation_handle handle;

  {
    auto awaiter =
        adapter::from_asio_safe(immediate_value(42), ctx.get_executor());
    handle = awaiter.get_cancellation_handle();
    CHECK(handle.valid());
  }

  CHECK_FALSE(handle.valid());
  CHECK_FALSE(handle.cancel());
}

TEST_CASE("asio async_simple safe adapter: multi-threaded io_context") {
  constexpr int task_count = 64;

  asio::io_context ctx;
  auto work_guard = asio::make_work_guard(ctx);
  std::atomic<int> completed{0};
  std::atomic<int> resume_count{0};
  std::vector<int> results(task_count, 0);
  std::vector<ConcurrentTask> tasks;
  tasks.reserve(task_count);

  std::thread t1([&] {
    ctx.run();
  });
  std::thread t2([&] {
    ctx.run();
  });

  for (int i = 0; i < task_count; ++i) {
    tasks.push_back(safe_concurrent_task(ctx.get_executor(), i, &results,
                                         &completed, &resume_count));
  }

  for (int i = 0; i < task_count; ++i) {
    asio::post(ctx, [&, i] {
      tasks[i].start();
    });
  }

  for (int i = 0;
       i < 1000 && completed.load(std::memory_order_acquire) != task_count;
       ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  CHECK(completed.load(std::memory_order_acquire) == task_count);
  work_guard.reset();
  t1.join();
  t2.join();

  CHECK(resume_count.load(std::memory_order_relaxed) == task_count);
  for (int i = 0; i < task_count; ++i) {
    CHECK(results[i] == i);
  }
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

#if ORMPP_ASIO_ASYNC_SIMPLE_ADAPTER_HAS_CANCELLATION
TEST_CASE("asio async_simple safe adapter: cancellation") {
  asio::io_context ctx;
  bool done = false;
  bool saw_cancel = false;
  std::function<bool()> cancel;
  auto task = safe_cancellation_task(ctx.get_executor(), &cancel, &saw_cancel);

  asio::post(ctx, [&] {
    task.start(done);
  });

  ctx.restart();
  REQUIRE(ctx.run_one() == 1);
  REQUIRE(cancel);
  CHECK(cancel());

  run_until(ctx, [&] {
    return done;
  });
  CHECK(saw_cancel);
  CHECK_NOTHROW(task.result());
}

TEST_CASE("asio async_simple safe adapter: cancel before start") {
  asio::io_context ctx;
  bool done = false;
  bool saw_cancel = false;
  auto task = safe_cancel_before_start_task(ctx.get_executor(), &saw_cancel);

  asio::post(ctx, [&] {
    task.start(done);
  });

  run_until(ctx, [&] {
    return done;
  });

  CHECK(saw_cancel);
  CHECK_NOTHROW(task.result());
}

TEST_CASE("asio async_simple safe adapter: cancel after completion") {
  asio::io_context ctx;
  bool done = false;
  auto task = safe_cancel_after_completion_task(ctx.get_executor());

  asio::post(ctx, [&] {
    task.start(done);
  });

  run_until(ctx, [&] {
    return done;
  });

  CHECK_NOTHROW(task.result());
}
#endif

#endif  // ORMPP_ENABLE_MYSQL_ASYNC
