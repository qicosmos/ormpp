#ifdef ORMPP_ENABLE_MYSQL_ASYNC

#include <asio.hpp>

#include <cstdlib>
#include <string>

#include "dbng.hpp"
#include "doctest.h"
#include "mysql_async.hpp"

using namespace ormpp;

namespace {

inline void require_async(bool cond, std::string_view msg) {
  if (!cond) {
    throw std::runtime_error(std::string(msg));
  }
}

struct async_person {
  int id;
  std::string name;
  int age;
};
REGISTER_AUTO_KEY(async_person, id)
YLT_REFL(async_person, id, name, age)

enum class async_color { blue = 10, red = 15 };
enum async_fruit { apple, banana };

struct async_optional_row {
  int id;
  std::optional<std::string> name;
  std::optional<int> age;
};
REGISTER_AUTO_KEY(async_optional_row, id)
YLT_REFL(async_optional_row, id, name, age)

struct async_chain_optional_row {
  int id;
  std::optional<std::string> name;
  std::optional<int> age;
  std::optional<int> empty_;
};
REGISTER_AUTO_KEY(async_chain_optional_row, id)
YLT_REFL(async_chain_optional_row, id, name, age, empty_)

struct async_chain_optional_subset {
  std::optional<std::string> name;
  std::optional<int> age;
};
YLT_REFL(async_chain_optional_subset, name, age)

struct async_enum_row {
  int id;
  async_color color;
  async_fruit fruit;
};
REGISTER_AUTO_KEY(async_enum_row, id)
YLT_REFL(async_enum_row, id, color, fruit)

struct async_bool_row {
  int id;
  bool ok;
};
REGISTER_AUTO_KEY(async_bool_row, id)
YLT_REFL(async_bool_row, id, ok)

struct async_string_view_row {
  uint8_t a;
  uint16_t b;
  uint32_t c;
  uint64_t d;
  int8_t e;
  int16_t f;
  int32_t g;
  int64_t h;
  std::string_view v;
  int id;
};
REGISTER_AUTO_KEY(async_string_view_row, id)
YLT_REFL(async_string_view_row, a, b, c, d, e, f, g, h, v, id)

struct async_blob_row {
  int id;
  ormpp::blob bin;
};
YLT_REFL(async_blob_row, id, bin)

struct async_blob_ex_row {
  int id;
  ormpp::blob bin;
  std::string time;
};
YLT_REFL(async_blob_ex_row, id, bin, time)

struct async_builder_person {
  std::string name;
  int age;
  int id;
  int score;
};
REGISTER_AUTO_KEY(async_builder_person, id)
YLT_REFL(async_builder_person, name, age, id, score)

namespace async_ns_test {
struct person_with_namespace {
  int id;
  std::string name;
  int age;
};
REGISTER_AUTO_KEY(person_with_namespace, id)
YLT_REFL(person_with_namespace, id, name, age)

struct department {
  int id;
  std::string name;
  int manager_id;
};
REGISTER_AUTO_KEY(department, id)
YLT_REFL(department, id, name, manager_id)
}  // namespace async_ns_test

inline std::string getenv_or(const char* key, const char* fallback) {
  if (auto* val = std::getenv(key); val != nullptr) {
    return val;
  }
  return fallback;
}

asio::awaitable<void> run_async_mysql_smoke() {
  auto executor = co_await asio::this_coro::executor;
  dbng<mysql_async> db(executor);

  auto host = getenv_or("ORMPP_ASYNC_MYSQL_HOST", "127.0.0.1");
  auto user = getenv_or("ORMPP_ASYNC_MYSQL_USER", "root");
  auto password = getenv_or("ORMPP_ASYNC_MYSQL_PASSWORD", "");
  auto database = getenv_or("ORMPP_ASYNC_MYSQL_DB", "test_ormppdb");
  auto port_str = getenv_or("ORMPP_ASYNC_MYSQL_PORT", "3306");
  auto port = std::stoi(port_str);

  auto connected = co_await db.connect(host, user, password, database, 5, port);
  if (!connected) {
    co_return;
  }
  require_async(co_await db.ping(), "async ping failed");

  require_async(co_await db.execute("drop table if exists async_person"),
                "drop table failed");
  require_async(co_await db.create_datatable<async_person>(ormpp_auto_key{"id"}),
                "create table failed");

  auto inserted =
      co_await db.insert(async_person{0, "async_alice", 18});
  require_async(inserted == 1, "insert returned unexpected affected rows");

  auto inserted_id =
      co_await db.get_insert_id_after_insert(async_person{0, "async_bob", 19});
  require_async(inserted_id > 0, "insert id should be positive");

  auto rows = co_await db.query_s<async_person>("order by id");
  require_async(rows.size() >= 2, "expected at least two rows");
  require_async(rows[0].name == "async_alice", "first row name mismatch");

  auto tuple_rows = co_await db.query_s<std::tuple<int, std::string>>(
      "select age, name from async_person where name=?", "async_bob");
  require_async(tuple_rows.size() == 1, "tuple query row count mismatch");
  require_async(std::get<0>(tuple_rows.front()) == 19,
                "tuple query age mismatch");
  require_async(std::get<1>(tuple_rows.front()) == "async_bob",
                "tuple query name mismatch");

  auto builder_rows = co_await db.select(all)
                          .from<async_person>()
                          .where(col(&async_person::name).param())
                          .collect(std::string("async_alice"));
  require_async(builder_rows.size() == 1, "builder collect row count mismatch");
  require_async(builder_rows.front().age == 18, "builder collect age mismatch");

  auto builder_rows_literal_task = db.select(all)
                                       .from<async_person>()
                                       .where(col(&async_person::name).param())
                                       .collect("async_alice");
  auto builder_rows_literal = co_await std::move(builder_rows_literal_task);
  require_async(builder_rows_literal.size() == 1,
                "builder literal collect row count mismatch");
  require_async(builder_rows_literal.front().age == 18,
                "builder literal collect age mismatch");

  auto scalar_age = co_await db.select(col(&async_person::age))
                            .from<async_person>()
                            .where(col(&async_person::name).param())
                            .scalar(std::string("async_bob"));
  require_async(scalar_age == 19, "scalar query mismatch");

  auto scalar_age_literal_task = db.select(col(&async_person::age))
                                   .from<async_person>()
                                   .where(col(&async_person::name).param())
                                   .scalar("async_bob");
  auto scalar_age_literal = co_await std::move(scalar_age_literal_task);
  require_async(scalar_age_literal == 19, "scalar literal query mismatch");

  auto replaced = co_await db.replace(async_person{
      static_cast<int>(inserted_id), "async_bob_replaced", 21});
  require_async(replaced >= 1, "replace failed");

  auto replaced_rows =
      co_await db.query_s<async_person>("id=?", static_cast<int>(inserted_id));
  require_async(replaced_rows.size() == 1, "replace query row count mismatch");
  require_async(replaced_rows.front().name == "async_bob_replaced",
                "replace value mismatch");

  auto updated =
      co_await db.update(async_person{static_cast<int>(inserted_id),
                                      "async_bob_updated", 22});
  require_async(updated == 1, "update failed");

  auto update_rows =
      co_await db.query_s<async_person>("id=?", static_cast<int>(inserted_id));
  require_async(update_rows.size() == 1, "update query row count mismatch");
  require_async(update_rows.front().age == 22, "update age mismatch");

  auto builder_update_rows = co_await db.update<async_person>()
                                  .set(col(&async_person::age), 30)
                                  .where(col(&async_person::id) ==
                                         static_cast<int>(inserted_id))
                                  .execute();
  require_async(builder_update_rows == 1, "builder update execute failed");

  auto after_builder_update =
      co_await db.query_s<async_person>("id=?", static_cast<int>(inserted_id));
  require_async(after_builder_update.front().age == 30,
                "builder update result mismatch");

  auto builder_delete_rows = co_await db.remove<async_person>()
                                  .where(col(&async_person::id) ==
                                         static_cast<int>(inserted_id))
                                  .execute();
  require_async(builder_delete_rows == 1, "builder delete execute failed");

  auto after_builder_delete =
      co_await db.query_s<async_person>("id=?", static_cast<int>(inserted_id));
  require_async(after_builder_delete.empty(),
                "builder delete did not remove row");

  require_async(co_await db.begin(), "begin failed");
  require_async(
      co_await db.execute(
          "insert into async_person(name, age) values('async_tx', 20)"),
      "transaction insert failed");
  require_async(co_await db.rollback(), "rollback failed");

  auto tx_rows = co_await db.query_s<async_person>("name=?", "async_tx");
  require_async(tx_rows.empty(), "rollback did not revert row");

  require_async(
      co_await db.delete_records_s<async_person>("name=?", "async_alice") == 1,
      "delete_records_s affected rows mismatch");
  auto deleted_rows =
      co_await db.query_s<async_person>("name=?", "async_alice");
  require_async(deleted_rows.empty(), "deleted row still visible");

  require_async(co_await db.execute("drop table if exists async_optional_row"),
                "drop optional table failed");
  require_async(
      co_await db.create_datatable<async_optional_row>(ormpp_auto_key{"id"}),
      "create optional table failed");
  require_async(co_await db.insert(async_optional_row{
                    0, std::optional<std::string>{"opt_name"},
                    std::optional<int>{88}}) == 1,
                "insert optional row failed");
  auto optional_rows = co_await db.query_s<async_optional_row>();
  require_async(optional_rows.size() == 1, "optional row count mismatch");
  require_async(optional_rows.front().name.has_value() &&
                    optional_rows.front().name.value() == "opt_name",
                "optional string mismatch");
  require_async(optional_rows.front().age.has_value() &&
                    optional_rows.front().age.value() == 88,
                "optional int mismatch");

  require_async(
      co_await db.execute("drop table if exists async_chain_optional_row"),
      "drop chain optional table failed");
  require_async(
      co_await db.create_datatable<async_chain_optional_row>(
          ormpp_auto_key{"id"}),
      "create chain optional table failed");
  require_async(co_await db.insert(async_chain_optional_row{
                    0, std::optional<std::string>{"purecpp"},
                    std::optional<int>{1}, std::nullopt}) == 1,
                "insert chain optional row 1 failed");
  require_async(co_await db.insert(async_chain_optional_row{
                    0, std::optional<std::string>{"test"},
                    std::optional<int>{2}, std::nullopt}) == 1,
                "insert chain optional row 2 failed");

  auto chain_param_rows = co_await db.select(all)
                              .from<async_chain_optional_row>()
                              .where(col(&async_chain_optional_row::id).param())
                              .collect(2);
  require_async(chain_param_rows.size() == 1 &&
                    chain_param_rows.front().name == std::optional<std::string>{"test"},
                "chain param collect mismatch");

  auto chain_subset_rows = co_await db
                               .select(col(&async_chain_optional_row::name),
                                       col(&async_chain_optional_row::age))
                               .from<async_chain_optional_row>()
                               .where(col(&async_chain_optional_row::id).param())
                               .collect<async_chain_optional_subset>(2);
  require_async(chain_subset_rows.size() == 1 &&
                    chain_subset_rows.front().name ==
                        std::optional<std::string>{"test"} &&
                    chain_subset_rows.front().age == std::optional<int>{2},
                "chain subset collect mismatch");

  auto grouped_rows = co_await db
                          .select(count(col(&async_chain_optional_row::id)),
                                  col(&async_chain_optional_row::id))
                          .from<async_chain_optional_row>()
                          .group_by(col(&async_chain_optional_row::id))
                          .having(sum(col(&async_chain_optional_row::age)) > 0 &&
                                  count() > 0)
                          .collect();
  require_async(grouped_rows.size() == 2, "group_by having row count mismatch");

  auto limit_offset_rows = co_await db.select(all)
                                .from<async_chain_optional_row>()
                                .where(col(&async_chain_optional_row::id).in(1, 2))
                                .order_by(col(&async_chain_optional_row::id).desc(),
                                          col(&async_chain_optional_row::name).desc())
                                .limit(token)
                                .offset(token)
                                .collect(5, 0);
  require_async(limit_offset_rows.size() == 2 &&
                    limit_offset_rows.front().id == 2 &&
                    limit_offset_rows.back().id == 1,
                "limit offset collect mismatch");

  auto between_rows = co_await db.select(all)
                           .from<async_chain_optional_row>()
                           .where(col(&async_chain_optional_row::name)
                                      .between("purecpp", "test"))
                           .collect();
  require_async(between_rows.size() == 2, "between collect mismatch");

  auto like_rows = co_await db.select(all)
                        .from<async_chain_optional_row>()
                        .where(col(&async_chain_optional_row::name).like("pure%"))
                        .collect();
  require_async(like_rows.size() == 1 &&
                    like_rows.front().name ==
                        std::optional<std::string>{"purecpp"},
                "like collect mismatch");

  require_async(co_await db.execute("drop table if exists async_enum_row"),
                "drop enum table failed");
  require_async(co_await db.create_datatable<async_enum_row>(ormpp_auto_key{"id"}),
                "create enum table failed");
  require_async(co_await db.insert(async_enum_row{0, async_color::blue, apple}) ==
                    1,
                "insert enum row failed");
  auto enum_rows = co_await db.query_s<async_enum_row>();
  require_async(enum_rows.size() == 1, "enum row count mismatch");
  require_async(enum_rows.front().color == async_color::blue,
                "enum color mismatch");
  require_async(enum_rows.front().fruit == apple, "enum fruit mismatch");

  require_async(co_await db.execute("drop table if exists async_bool_row"),
                "drop bool table failed");
  require_async(co_await db.create_datatable<async_bool_row>(ormpp_auto_key{"id"}),
                "create bool table failed");
  require_async(co_await db.insert(async_bool_row{0, true}) == 1,
                "insert bool true failed");
  auto bool_rows = co_await db.query_s<async_bool_row>();
  require_async(bool_rows.size() == 1 && bool_rows.front().ok,
                "bool true mismatch");
  require_async(co_await db.delete_records_s<async_bool_row>() == 1,
                "delete bool rows failed");
  require_async(co_await db.insert(async_bool_row{0, false}) == 1,
                "insert bool false failed");
  bool_rows = co_await db.query_s<async_bool_row>();
  require_async(bool_rows.size() == 1 && !bool_rows.front().ok,
                "bool false mismatch");

  require_async(
      co_await db.execute("drop table if exists async_string_view_row"),
      "drop string_view table failed");
  require_async(
      co_await db.create_datatable<async_string_view_row>(ormpp_auto_key{"id"}),
      "create string_view table failed");
  auto string_view_id = co_await db.get_insert_id_after_insert(
      async_string_view_row{1, 2, 3, 4, 5, 6, 7, 8, "purecpp"});
  require_async(string_view_id > 0, "insert string_view row failed");
  auto string_view_rows = co_await db.query_s<async_string_view_row>();
  require_async(string_view_rows.size() == 1,
                "string_view row count mismatch");
  require_async(string_view_rows.front().a == 1 &&
                    string_view_rows.front().b == 2 &&
                    string_view_rows.front().c == 3 &&
                    string_view_rows.front().d == 4 &&
                    string_view_rows.front().e == 5 &&
                    string_view_rows.front().f == 6 &&
                    string_view_rows.front().g == 7 &&
                    string_view_rows.front().h == 8,
                "integer mapping mismatch");
  require_async(string_view_rows.front().v == "purecpp",
                "string_view mapping mismatch");

  require_async(co_await db.execute("drop table if exists async_blob_row"),
                "drop blob table failed");
  require_async(co_await db.create_datatable<async_blob_row>(),
                "create blob table failed");
  {
    auto data = "this is a test binary stream\0, and ?...";
    auto size = 40;
    async_blob_row blob_row{1};
    blob_row.bin.assign(data, data + size);
    require_async(co_await db.insert(blob_row) == 1, "insert blob row failed");
    auto blob_rows = co_await db.query_s<async_blob_row>("id=?", 1);
    require_async(blob_rows.size() == 1, "blob row count mismatch");
    require_async(blob_rows.front().bin.size() == static_cast<size_t>(size),
                  "blob size mismatch");
  }

  require_async(co_await db.execute("drop table if exists async_blob_ex_row"),
                "drop blob ex table failed");
  require_async(co_await db.create_datatable<async_blob_ex_row>(),
                "create blob ex table failed");
  {
    auto data = "this is a test binary stream\0, and ?...";
    auto size = 40;
    async_blob_ex_row blob_ex{1};
    blob_ex.bin.assign(data, data + size);
    blob_ex.time = "2023-03-29 13:55:00";
    require_async(co_await db.insert(blob_ex) == 1, "insert blob ex row failed");
    auto blob_ex_rows = co_await db.query_s<async_blob_ex_row>("id=?", 1);
    require_async(blob_ex_rows.size() == 1, "blob ex row count mismatch");
    require_async(blob_ex_rows.front().bin.size() == static_cast<size_t>(size),
                  "blob ex size mismatch");
    using async_blob_tuple = std::tuple<async_blob_row, std::string>;
    auto tuple_blob_rows = co_await db.query_s<async_blob_tuple>(
        "select id,bin,time from async_blob_ex_row where id=?", 1);
    require_async(tuple_blob_rows.size() == 1, "blob tuple row count mismatch");
    require_async(std::get<0>(tuple_blob_rows.front()).id == 1,
                  "blob tuple id mismatch");
    require_async(std::get<1>(tuple_blob_rows.front()) == blob_ex.time,
                  "blob tuple time mismatch");
    require_async(std::get<0>(tuple_blob_rows.front()).bin.size() ==
                      static_cast<size_t>(size),
                  "blob tuple size mismatch");
  }

  require_async(co_await db.execute("drop table if exists async_builder_person"),
                "drop builder table failed");
  require_async(co_await db.create_table<async_builder_person>()
                    .auto_increment(col(&async_builder_person::id))
                    .not_null(col(&async_builder_person::name))
                    .execute(),
                "create_table builder failed");
  require_async(co_await db.insert(async_builder_person{"builder", 10, 0, 99}) ==
                    1,
                "insert builder row failed");
  auto builder_created_rows =
      co_await db.query_s<async_builder_person>("name=?", "builder");
  require_async(builder_created_rows.size() == 1,
                "builder create query row count mismatch");

  require_async(co_await db.alter_table<async_builder_person>()
                    .add_column("nickname", "TEXT")
                    .execute(),
                "alter_table add_column failed");
  require_async(
      co_await db.execute(
          "update async_builder_person set nickname='nick' where name='builder'"),
      "alter_table update nickname failed");
  auto nickname_rows = co_await db.query_s<std::tuple<std::string>>(
      "select nickname from async_builder_person where name=?", "builder");
  require_async(nickname_rows.size() == 1 &&
                    std::get<0>(nickname_rows.front()) == "nick",
                "alter_table nickname mismatch");

  require_async(
      co_await db.execute("drop table if exists person_with_namespace"),
      "drop namespaced person table failed");
  require_async(
      co_await db.create_datatable<async_ns_test::person_with_namespace>(
          ormpp_auto_key{"id"}),
      "create namespaced person table failed");
  require_async(co_await db.insert(async_ns_test::person_with_namespace{
                    0, "tom", 18}) == 1,
                "insert namespaced person tom failed");
  require_async(co_await db.insert(async_ns_test::person_with_namespace{
                    0, "jerry", 20}) == 1,
                "insert namespaced person jerry failed");
  require_async(co_await db.insert(async_ns_test::person_with_namespace{
                    0, "mike", 22}) == 1,
                "insert namespaced person mike failed");

  auto ns_param_rows = co_await db.select(all)
                             .from<async_ns_test::person_with_namespace>()
                             .where(col(&async_ns_test::person_with_namespace::id)
                                        .param())
                             .collect(2);
  require_async(ns_param_rows.size() == 1 &&
                    ns_param_rows.front().name == "jerry",
                "namespaced param collect mismatch");

  auto ns_where_rows = co_await db.select(all)
                             .from<async_ns_test::person_with_namespace>()
                             .where(col(&async_ns_test::person_with_namespace::age) >
                                    18)
                             .collect();
  require_async(ns_where_rows.size() == 2, "namespaced where collect mismatch");

  auto ns_all_rows =
      co_await db.select(all).from<async_ns_test::person_with_namespace>().collect();
  require_async(ns_all_rows.size() == 3, "namespaced all collect mismatch");

  require_async(co_await db.execute("drop table if exists department"),
                "drop namespaced department table failed");
  require_async(
      co_await db.create_datatable<async_ns_test::department>(
          ormpp_auto_key{"id"}),
      "create namespaced department table failed");
  require_async(
      co_await db.insert(async_ns_test::department{0, "Engineering", 1}) == 1,
      "insert department 1 failed");
  require_async(co_await db.insert(async_ns_test::department{0, "Sales", 2}) ==
                    1,
                "insert department 2 failed");
  require_async(co_await db.insert(async_ns_test::department{0, "HR", 3}) == 1,
                "insert department 3 failed");

  auto join_rows =
      co_await db
          .select(col(&async_ns_test::person_with_namespace::name),
                  col(&async_ns_test::department::name))
          .from<async_ns_test::person_with_namespace>()
          .inner_join(col(&async_ns_test::person_with_namespace::id),
                      col(&async_ns_test::department::manager_id))
          .collect();
  require_async(join_rows.size() == 3, "namespaced join row count mismatch");

  require_async(co_await db.disconnect(), "disconnect failed");
}

}  // namespace

TEST_CASE("mysql async smoke") {
  asio::io_context ctx;
  auto fut = asio::co_spawn(ctx, run_async_mysql_smoke(), asio::use_future);
  CHECK_NOTHROW(ctx.run());
  CHECK_NOTHROW(fut.get());
}

#endif
