#include "postgresql_async.hpp"

#include <asio.hpp>
#include <cstdlib>
#include <iostream>
#include <stdexcept>

#include "dbng.hpp"

struct pg_async_example_person {
  int id;
  std::string name;
  int age;
};
REGISTER_AUTO_KEY(pg_async_example_person, id)
YLT_REFL(pg_async_example_person, id, name, age)

std::string setting(const char* name, const char* fallback) {
  const auto* value = std::getenv(name);
  return value ? value : fallback;
}

asio::awaitable<void> example() {
  ormpp::dbng<ormpp::postgresql_async> db(co_await asio::this_coro::executor);
  if (!(co_await db.connect(
          setting("PGHOST", "127.0.0.1"), setting("PGUSER", "root"),
          setting("PGPASSWORD", ""), setting("PGDATABASE", "test_ormppdb"), 5,
          std::stoi(setting("PGPORT", "5432"))))) {
    throw std::runtime_error(db.get_last_error());
  }
  if (!(co_await db.create_datatable<pg_async_example_person>())) {
    throw std::runtime_error(db.get_last_error());
  }
  const auto id = co_await db.get_insert_id_after_insert(
      pg_async_example_person{0, "alice", 20});
  if (id == 0) {
    throw std::runtime_error(db.get_last_error());
  }
  auto rows = co_await db.query_s<pg_async_example_person>("id=$1", id);
  if (db.has_error()) {
    throw std::runtime_error(db.get_last_error());
  }
  for (const auto& row : rows) {
    std::cout << row.id << ": " << row.name << ", " << row.age << '\n';
  }
  co_await db.disconnect();
}

int main() {
  asio::io_context io;
  auto done = asio::co_spawn(io, example(), asio::use_future);
  io.run();
  try {
    done.get();
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
