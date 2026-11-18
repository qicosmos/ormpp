#ifdef ORMPP_ENABLE_MYSQL
#include "mysql.hpp"
#endif
#ifdef ORMPP_ENABLE_PG
#include "postgresql.hpp"
#endif
#include <algorithm>
#include <array>
#include <string>
#include <vector>

#include "dbng.hpp"
#include "doctest.h"
#include "sqlite.hpp"

using namespace ormpp;

namespace {

struct list_order_log {
  int id;
  int order_id;
  std::string payload;
};

struct list_order_code {
  int id;
  std::array<char, 32> order_code;
};

template <typename Test>
void with_partition_databases(Test test) {
#ifdef ORMPP_ENABLE_PG
  const char* password = "123456";
#else
  const char* password = "";
#endif
#ifdef ORMPP_ENABLE_MYSQL
  SUBCASE("mysql") {
    dbng<mysql> database;
    REQUIRE(database.connect("127.0.0.1", "root", password, "test_ormppdb"));
    test(database, std::integral_constant<DBType, DBType::mysql>{});
  }
#endif
#ifdef ORMPP_ENABLE_PG
  SUBCASE("postgresql") {
    dbng<postgresql> database;
    REQUIRE(database.connect("127.0.0.1", "root", password, "test_ormppdb"));
    test(database, std::integral_constant<DBType, DBType::postgresql>{});
  }
#endif
  SUBCASE("sqlite") {
    dbng<sqlite> database;
#ifdef SQLITE_HAS_CODEC
    REQUIRE(database.connect(":memory:", "123456"));
#else
    REQUIRE(database.connect(":memory:"));
#endif
    test(database, std::integral_constant<DBType, DBType::sqlite>{});
  }
}

template <DBType Type>
struct partition_sql_recorder {
  static constexpr DBType db_type_v = Type;
  std::vector<std::string> sqls;

  bool execute(const std::string& sql) {
    sqls.push_back(sql);
    return true;
  }
  int get_last_affect_rows() const { return 0; }
};

}  // namespace

TEST_CASE("builder list partition interfaces") {
  with_partition_databases([](auto& database, auto type) {
    constexpr auto db_type = decltype(type)::value;
    auto orders = list_partition("p_orders", std::vector<int>{1001, 1003});
    auto current = list_partition("p_current", 2001, 2003);
    auto added = list_partition("p_added", {3001, 3005});
    auto count_rows = [&] {
      return database.select(count()).template from<list_order_log>().collect();
    };

    REQUIRE(database.execute(
        "DROP TABLE IF EXISTS list_order_log" +
        std::string(db_type == DBType::postgresql ? " CASCADE" : "")));
    REQUIRE(database.template create_table<list_order_log>()
                .primary_key(col(&list_order_log::id),
                             col(&list_order_log::order_id))
                .partition_by_list(col(&list_order_log::order_id))
                .partition(orders)
                .list_partition("p_current", {2001, 2003})
                .execute());
    REQUIRE(database.insert(list_order_log{1, 1001, "remove"}) == 1);
    REQUIRE(database.insert(list_order_log{2, 1003, "keep"}) == 1);
    REQUIRE(database.insert(list_order_log{3, 2001, "remove"}) == 1);
    REQUIRE(database.insert(list_order_log{4, 2003, "keep"}) == 1);

    CHECK(database.template remove<list_order_log>()
              .partition(col(&list_order_log::order_id), orders)
              .where(col(&list_order_log::payload) == "remove" ||
                     col(&list_order_log::id) == 4)
              .execute() == 1);
    CHECK(count_rows() == 3);

    REQUIRE(database.template alter_table<list_order_log>()
                .clear_partition(col(&list_order_log::order_id), current)
                .execute());
    CHECK(count_rows() == 1);
    auto remaining =
        database.select(all).template from<list_order_log>().collect();
    REQUIRE(remaining.size() == 1);
    CHECK(remaining.front().order_id == 1003);

    REQUIRE(database.template alter_table<list_order_log>()
                .add_partition(col(&list_order_log::order_id), added)
                .execute());
    REQUIRE(database.insert(list_order_log{5, 3001, "new"}) == 1);
    REQUIRE(database.insert(list_order_log{6, 3005, "new"}) == 1);
    CHECK(database.template remove<list_order_log>()
              .partition(col(&list_order_log::order_id), added)
              .execute_all() == 2);
    CHECK(count_rows() == 1);

    REQUIRE(database.insert(list_order_log{7, 3005, "new"}) == 1);
    if constexpr (db_type == DBType::sqlite) {
      CHECK_FALSE(database.template alter_table<list_order_log>()
                      .drop_partition(added)
                      .execute());
      CHECK(count_rows() == 2);
    }
    else {
      REQUIRE(database.template alter_table<list_order_log>()
                  .drop_partition(added)
                  .execute());
      CHECK(count_rows() == 1);
    }
    REQUIRE(database.execute(
        "DROP TABLE list_order_log" +
        std::string(db_type == DBType::postgresql ? " CASCADE" : "")));
  });
}

TEST_CASE("builder list partitions support strings and null") {
  with_partition_databases([](auto& database, auto type) {
    constexpr auto db_type = decltype(type)::value;
    auto selected = list_partition("p_selected", nullptr, "order'one");
    auto insert_code = [&](int id, std::string_view code) {
      list_order_code row{id, {}};
      std::copy(code.begin(), code.end(), row.order_code.begin());
      REQUIRE(database.insert(row) == 1);
    };
    auto count_rows = [&] {
      return database.select(count())
          .template from<list_order_code>()
          .collect();
    };

    REQUIRE(database.execute(
        "DROP TABLE IF EXISTS list_order_code" +
        std::string(db_type == DBType::postgresql ? " CASCADE" : "")));
    REQUIRE(database.template create_table<list_order_code>()
                .partition_by_list(col(&list_order_code::order_code))
                .partition(selected)
                .list_partition("p_other", "other", "NULL")
                .execute());
    insert_code(1, "order'one");
    insert_code(2, "other");
    REQUIRE(database.execute("INSERT INTO list_order_code VALUES (3,NULL)"));
    insert_code(4, "NULL");
    CHECK(count_rows() == 4);

    CHECK(database.template remove<list_order_code>()
              .partition(col(&list_order_code::order_code), selected)
              .where(col(&list_order_code::id) == 2 ||
                     col(&list_order_code::id) == 3)
              .execute() == 1);
    CHECK(count_rows() == 3);
    REQUIRE(database.template alter_table<list_order_code>()
                .clear_partition(col(&list_order_code::order_code), selected)
                .execute());
    CHECK(count_rows() == 2);
    REQUIRE(database.execute(
        "DROP TABLE list_order_code" +
        std::string(db_type == DBType::postgresql ? " CASCADE" : "")));
  });
}

TEST_CASE("builder list partition dialects") {
  auto partition = list_partition("p_orders", 1001, 1003);
  partition_sql_recorder<DBType::mysql> mysql_db;
  // Use recorders directly so both native dialects are checked in SQLite
  // builds.
  REQUIRE(make_create_table_builder<list_order_log>(&mysql_db)
              .partition_by_list(col(&list_order_log::order_id))
              .partition(partition)
              .execute());
  REQUIRE(mysql_db.sqls.size() == 1);
  CHECK(
      mysql_db.sqls.front().find(
          " PARTITION BY LIST COLUMNS(order_id) (PARTITION p_orders VALUES IN "
          "(1001,1003))") != std::string::npos);

  partition_sql_recorder<DBType::postgresql> postgres;
  REQUIRE(make_create_table_builder<list_order_log>(&postgres)
              .partition_by_list(col(&list_order_log::order_id))
              .partition(partition)
              .execute());
  REQUIRE(postgres.sqls.size() == 2);
  CHECK(postgres.sqls.front().find(" PARTITION BY LIST (order_id)") !=
        std::string::npos);
  CHECK(postgres.sqls.back() ==
        "CREATE TABLE IF NOT EXISTS list_order_log_p_orders PARTITION OF "
        "list_order_log FOR VALUES IN (1001,1003)");
  REQUIRE(make_alter_table_builder<list_order_log>(&postgres)
              .clear_partition(col(&list_order_log::order_id), partition)
              .add_partition(col(&list_order_log::order_id),
                             list_partition("p_added", 3001))
              .drop_partition(partition)
              .execute());
  REQUIRE(postgres.sqls.size() == 5);
  CHECK(postgres.sqls[2] == "TRUNCATE TABLE list_order_log_p_orders");
  CHECK(postgres.sqls[3] ==
        "CREATE TABLE IF NOT EXISTS list_order_log_p_added PARTITION OF "
        "list_order_log FOR VALUES IN (3001)");
  CHECK(postgres.sqls[4] == "DROP TABLE IF EXISTS list_order_log_p_orders");

  const auto condition = col(&list_order_log::payload) == "remove" ||
                         col(&list_order_log::id) == 4;
  CHECK(make_delete_builder<list_order_log>(&postgres)
            .partition(col(&list_order_log::order_id), partition)
            .where(condition)
            .execute() == 0);
  CHECK(postgres.sqls.back() ==
        "DELETE FROM list_order_log_p_orders AS list_order_log WHERE " +
            condition.to_sql());

  partition_sql_recorder<DBType::sqlite> sqlite_db;
  REQUIRE(make_alter_table_builder<list_order_code>(&sqlite_db)
              .clear_partition(col(&list_order_code::order_code),
                               list_partition("p_null", std::nullopt))
              .execute());
  REQUIRE(sqlite_db.sqls.size() == 1);
  CHECK(sqlite_db.sqls.front() ==
        "DELETE FROM list_order_code WHERE order_code IS NULL");

  // Keep existing calls that initialize a range descriptor with braces valid.
  REQUIRE(make_alter_table_builder<list_order_log>(&mysql_db)
              .clear_partition(col(&list_order_log::order_id),
                               {"p_range", "1001", "1004"})
              .add_partition(col(&list_order_log::order_id),
                             {"p_next_range", "1004", "1006"})
              .drop_partition({"p_next_range", "1004", "1006"})
              .execute());
  CHECK(make_delete_builder<list_order_log>(&mysql_db)
            .partition(col(&list_order_log::order_id),
                       {"p_range", "1001", "1004"})
            .execute_all() == 0);
  CHECK(mysql_db.sqls.back() ==
        "DELETE FROM list_order_log PARTITION (p_range)");
}

TEST_CASE("builder rejects invalid list partitions before executing SQL") {
  partition_sql_recorder<DBType::mysql> database;
  const auto field = col(&list_order_log::order_id);
  const std::vector<list_partition_desc> invalid{
      list_partition("empty"), list_partition("bad-name", 1),
      list_partition("1invalid", 1), list_partition("empty_sql", raw_sql("")),
      list_partition("duplicate_values", 1, 1)};
  for (const auto& partition : invalid) {
    CHECK_FALSE(make_create_table_builder<list_order_log>(&database)
                    .partition_by_list(field)
                    .partition(partition)
                    .execute());
    CHECK(make_delete_builder<list_order_log>(&database)
              .partition(field, partition)
              .execute_all() == -1);
    CHECK_FALSE(make_alter_table_builder<list_order_log>(&database)
                    .clear_partition(field, partition)
                    .execute());
    CHECK_FALSE(make_alter_table_builder<list_order_log>(&database)
                    .add_partition(field, partition)
                    .execute());
    CHECK_FALSE(make_alter_table_builder<list_order_log>(&database)
                    .drop_partition(partition)
                    .execute());
  }
  CHECK_FALSE(make_create_table_builder<list_order_log>(&database)
                  .partition_by_list(field)
                  .execute());
  CHECK_FALSE(make_create_table_builder<list_order_log>(&database)
                  .partition(list_partition("p_orders", 1))
                  .execute());
  CHECK_FALSE(make_create_table_builder<list_order_log>(&database)
                  .partition_by_list(field)
                  .partition_by_range(field)
                  .list_partition("p_orders", 1)
                  .execute());
  CHECK_FALSE(make_create_table_builder<list_order_log>(&database)
                  .partition_by_list(field)
                  .partition(range_partition("p_range", 1, 2))
                  .execute());
  CHECK_FALSE(make_create_table_builder<list_order_log>(&database)
                  .partition_by_range(field)
                  .list_partition("p_orders", 1)
                  .execute());
  CHECK_FALSE(make_create_table_builder<list_order_log>(&database)
                  .partition_by_list(field)
                  .list_partition("duplicate", 1)
                  .list_partition("duplicate", 2)
                  .execute());
  CHECK_FALSE(make_create_table_builder<list_order_log>(&database)
                  .partition_by_list(field)
                  .list_partition("p_orders", 1, 3)
                  .list_partition("p_overlap", 3, 5)
                  .execute());
  CHECK(database.sqls.empty());
}

#ifdef ORMPP_ENABLE_PG
TEST_CASE("postgresql binds list partition text keys within input bounds") {
  dbng<postgresql> database;
  REQUIRE(database.connect("127.0.0.1", "root", "123456", "test_ormppdb"));
  auto check_text = [&](const auto& value, const std::string& expected) {
    auto rows = database.query_s<std::tuple<std::string>>("SELECT $1::text", value);
    REQUIRE(rows.size() == 1);
    CHECK(std::get<0>(rows.front()) == expected);
  };

  const std::array<char, 4> full_key{'o', 'r', 'd', '1'};
  check_text(full_key, "ord1");
  check_text(std::string_view(full_key.data(), full_key.size()), "ord1");
  check_text(std::string_view(full_key.data() + 1, 2), "rd");
  check_text(std::array<char, 0>{}, "");
  check_text(std::string_view{}, "");
  check_text(std::string{}, "");
}
#endif
