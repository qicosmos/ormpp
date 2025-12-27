#ifdef ORMPP_ENABLE_MYSQL
#include "mysql.hpp"
#endif

#ifdef ORMPP_ENABLE_SQLITE3
#include "sqlite.hpp"
#endif

#ifdef ORMPP_ENABLE_PG
#include <thread>

#include "postgresql.hpp"
#endif

#include "connection_pool.hpp"
#include "dbng.hpp"
#include "doctest.h"
#include "ormpp_cfg.hpp"

using namespace std::string_literals;

using namespace ormpp;
#ifdef ORMPP_ENABLE_PG
const char *password = "123456";
#elif defined(ORMPP_ENABLE_SQLITE3) && defined(SQLITE_HAS_CODEC)
const char *password = "123456";
#else
const char *password = "";
#endif
const char *ip = "127.0.0.1";
const char *username = "root";
const char *db = "test_ormppdb";

struct person {
  std::string name;
  int age;
  int id;
};
REGISTER_AUTO_KEY(person, id)

struct student {
  int code;
  std::string name;
  char sex;
  int age;
  double dm;
  std::string classroom;
};
REGISTER_CONFLICT_KEY(student, code)

struct simple {
  int id;
  double code;
  int age;
  std::array<char, 128> arr;
};

namespace test_ns {
struct message_clear {
  int64_t room_id;
  int64_t user_id;
  int64_t message_id;
  int64_t created_at;
  int64_t updated_at;
  static constexpr std::string_view get_alias_struct_name(message_clear *) {
    return "im_message_clear";
  }
};
REGISTER_CONFLICT_KEY(message_clear, room_id, user_id)
}  // namespace test_ns

TEST_CASE("test update with multiple conflict keys") {
#ifdef ORMPP_ENABLE_MYSQL
  using namespace test_ns;
  dbng<mysql> mysql;
  if (mysql.connect(ip, username, password, db)) {
    mysql.execute("drop table if exists im_message_clear");
    mysql.create_datatable<message_clear>();
    message_clear data = {1, 1, 0, 0, 0};
    mysql.insert(data);
    auto clear = mysql.query_s<message_clear>("room_id=? and user_id=?", 1, 1);
    if (clear.size() == 1) {
      clear.front().message_id = 2;
      clear.front().updated_at = 0;
      mysql.update(clear.front());

      clear = mysql.query_s<message_clear>("room_id=? and user_id=?", 1, 1);
      auto &t = clear.front();
      CHECK(t.message_id == 2);
    }
  }
#endif
}

// TEST_CASE(mysql performance){
//    dbng<mysql> mysql;
//
//    REQUIRE(mysql.connect(ip, username, password, db));
//    REQUIRE(mysql.execute("DROP TABLE IF EXISTS student"));
//
//    ormpp_auto_increment_key auto_key{"code"};
//    REQUIRE(mysql.create_datatable<student>(auto_key));
//
//    using namespace std::chrono;
//    auto m_begin = high_resolution_clock::now();
//    for (int i = 0; i < 10000; ++i) {
//        mysql.insert(student{i, "tom", 0, i, 1.5, "classroom1"});
//    }
//    auto s = duration_cast<duration<double>>(high_resolution_clock::now() -
//    m_begin).count(); std::cout<<s<<'\n';
//
//    m_begin = high_resolution_clock::now();
//    std::vector<student> v;
//    for (int i = 0; i < 10000; ++i) {
//        v.push_back(student{i, "tom", 0, i, 1.5, "classroom1"});
//    }
//    mysql.insert(v);
//    s = duration_cast<duration<double>>(high_resolution_clock::now() -
//    m_begin).count(); std::cout<<s<<'\n';
//
//    m_begin = high_resolution_clock::now();
//    for (int j = 0; j < 100; ++j) {
//        REQUIRE(!mysql.query_s<student>("limit 1000").empty());
//    }
//    s = duration_cast<duration<double>>(high_resolution_clock::now() -
//    m_begin).count(); std::cout<<s<<'\n';
//}

template <class T, size_t N>
constexpr size_t size(T (&)[N]) {
  return N;
}

struct test_optional {
  int id;
  std::optional<std::string> name;
  std::optional<int> age;
  std::optional<int> empty_;
};
REGISTER_AUTO_KEY(test_optional, id)

TEST_CASE("test client pool") {
#ifdef ORMPP_ENABLE_MYSQL
  auto &pool = connection_pool<dbng<mysql>>::instance();
  pool.init(4, ip, username, password, db, 5, 3306);
  size_t init_size = pool.size();
  CHECK(init_size == 4);
  {
    auto conn = pool.get();
    init_size = pool.size();
    CHECK(init_size == 3);
    auto conn2 = pool.get();
    init_size = pool.size();
    CHECK(init_size == 2);
    conn = nullptr;
    init_size = pool.size();
    CHECK(init_size == 3);
  }
  init_size = pool.size();
  CHECK(init_size == 4);
#endif
}

TEST_CASE("optional") {
#ifdef ORMPP_ENABLE_MYSQL
  dbng<mysql> mysql;
  if (mysql.connect(ip, username, password, db)) {
    mysql.execute("drop table if exists test_optional;");
    mysql.create_datatable<test_optional>(ormpp_auto_key{"id"});
    mysql.insert<test_optional>({0, "purecpp", 200});
    mysql.insert<test_optional>({0, "test", 300});
    {
      auto l =
          mysql.select_all().from<test_optional>().count().collect<uint64_t>();
      auto l2 = mysql.select_all()
                    .from<test_optional>()
                    .count(col(&test_optional::id))
                    .collect<uint64_t>();
      auto l3 = mysql.select_all()
                    .from<test_optional>()
                    .count_distinct(col(&test_optional::id))
                    .collect<uint64_t>();
      CHECK(l == 2);
      CHECK(l2 == 2);
      CHECK(l3 == 2);
    }
    auto l1 = mysql.select_all()
                  .from<test_optional>()
                  .where(col(&test_optional::id).in(1, 2))
                  .order_by(col(&test_optional::id).desc())
                  .limit(5)
                  .offset(0)
                  .collect();
    auto ll1 = mysql.select_all()
                   .from<test_optional>()
                   .where(col(&test_optional::id).not_in(1, 2))
                   .collect();
    auto ll2 = mysql.select_all()
                   .from<test_optional>()
                   .where(col(&test_optional::id).null())
                   .collect();
    auto ll3 = mysql.select_all()
                   .from<test_optional>()
                   .where(col(&test_optional::name).not_null())
                   .collect();
    CHECK(ll1.size() == 0);
    CHECK(ll2.size() == 0);
    CHECK(ll3.size() == 2);

    auto l2 = mysql.select_all()
                  .from<test_optional>()
                  .where(col(&test_optional::name).in("test", "purecpp"))
                  .collect();
    CHECK(l1.size() == 2);
    CHECK(l2.size() == 2);

    auto l3 = mysql.select_all()
                  .from<test_optional>()
                  .where(col(&test_optional::id).between(1, 2))
                  .collect();

    auto l4 = mysql.select_all()
                  .from<test_optional>()
                  .where(col(&test_optional::name).between("purecpp", "test"))
                  .collect();
    auto l5 = mysql.select_all()
                  .from<test_optional>()
                  .where(col(&test_optional::name).like("pure%"))
                  .collect();
    CHECK(l3.size() == 2);
    CHECK(l4.size() == 2);
    CHECK(l5.size() == 1);
    auto list =
        mysql.select_all()
            .from<test_optional>()
            .where(col(&test_optional::id) == 1 || col(&test_optional::id) == 2)
            .collect();
    REQUIRE(list.size() == 2);
    auto list1 = mysql.select_all().from<test_optional>().collect();
    REQUIRE(list1.size() == 2);
    auto list2 = mysql.select_all()
                     .from<test_optional>()
                     .where(col(&test_optional::id) == 2)
                     .collect();
    REQUIRE(list2.size() == 1);
    auto list3 = mysql.select_all()
                     .from<test_optional>()
                     .where(col(&test_optional::name) == "test")
                     .collect();
    REQUIRE(list3.size() == 1);

    auto vec1 = mysql.query_s<test_optional>();
    REQUIRE(vec1.size() > 0);
    CHECK(vec1.front().age.value() == 200);
    CHECK(vec1.front().name.value() == "purecpp");
    CHECK(vec1.front().empty_.has_value() == false);
    auto vec2 = mysql.query_s<test_optional>("select * from test_optional;");
    REQUIRE(vec2.size() > 0);
    CHECK(vec2.front().age.value() == 200);
    CHECK(vec2.front().name.value() == "purecpp");
    CHECK(vec2.front().empty_.has_value() == false);

    auto vec3 = mysql.query_s<test_optional>();
    REQUIRE(vec3.size() > 0);
    CHECK(vec3.front().age.value() == 200);
    CHECK(vec3.front().name.value() == "purecpp");
    CHECK(vec3.front().empty_.has_value() == false);
    auto vec4 = mysql.query_s<test_optional>("select * from test_optional;");
    REQUIRE(vec4.size() > 0);
    CHECK(vec4.front().age.value() == 200);
    CHECK(vec4.front().name.value() == "purecpp");
    CHECK(vec4.front().empty_.has_value() == false);
  }
#endif
#ifdef ORMPP_ENABLE_PG
  dbng<postgresql> postgres;
  if (postgres.connect(ip, username, password, db)) {
    postgres.execute("drop table if exists test_optional;");
    postgres.create_datatable<test_optional>(ormpp_auto_key{"id"});
    postgres.insert<test_optional>({0, "purecpp", 200});
    postgres.insert<test_optional>({0, "test", 300});
    {
      auto l = postgres.select_all()
                   .from<test_optional>()
                   .count()
                   .collect<uint64_t>();
      auto l2 = postgres.select_all()
                    .from<test_optional>()
                    .count(col(&test_optional::id))
                    .collect<uint64_t>();
      auto l3 = postgres.select_all()
                    .from<test_optional>()
                    .count_distinct(col(&test_optional::id))
                    .collect<uint64_t>();
      CHECK(l == 2);
      CHECK(l2 == 2);
      CHECK(l3 == 2);
    }
    auto l1 = postgres.select_all()
                  .from<test_optional>()
                  .where(col(&test_optional::id).in(1, 2))
                  .order_by(col(&test_optional::id).desc())
                  .limit(5)
                  .offset(0)
                  .collect();
    auto ll1 = postgres.select_all()
                   .from<test_optional>()
                   .where(col(&test_optional::id).not_in(1, 2))
                   .collect();
    auto ll2 = postgres.select_all()
                   .from<test_optional>()
                   .where(col(&test_optional::id).null())
                   .collect();
    auto ll3 = postgres.select_all()
                   .from<test_optional>()
                   .where(col(&test_optional::name).not_null())
                   .collect();
    CHECK(ll1.size() == 0);
    CHECK(ll2.size() == 0);
    CHECK(ll3.size() == 2);

    auto l2 = postgres.select_all()
                  .from<test_optional>()
                  .where(col(&test_optional::name).in("test", "purecpp"))
                  .collect();
    CHECK(l1.size() == 2);
    CHECK(l2.size() == 2);

    auto l3 = postgres.select_all()
                  .from<test_optional>()
                  .where(col(&test_optional::id).between(1, 2))
                  .collect();

    auto l4 = postgres.select_all()
                  .from<test_optional>()
                  .where(col(&test_optional::name).between("purecpp", "test"))
                  .collect();
    auto l5 = postgres.select_all()
                  .from<test_optional>()
                  .where(col(&test_optional::name).like("pure%"))
                  .collect();
    CHECK(l3.size() == 2);
    CHECK(l4.size() == 2);
    CHECK(l5.size() == 1);
    auto list =
        postgres.select_all()
            .from<test_optional>()
            .where(col(&test_optional::id) == 1 || col(&test_optional::id) == 2)
            .collect();
    REQUIRE(list.size() == 2);
    auto list1 = postgres.select_all().from<test_optional>().collect();
    REQUIRE(list1.size() == 2);
    auto list2 = postgres.select_all()
                     .from<test_optional>()
                     .where(col(&test_optional::id) == 2)
                     .collect();
    REQUIRE(list2.size() == 1);
    auto list3 = postgres.select_all()
                     .from<test_optional>()
                     .where(col(&test_optional::name) == "test")
                     .collect();
    REQUIRE(list3.size() == 1);

    auto vec1 = postgres.query_s<test_optional>();
    REQUIRE(vec1.size() > 0);
    CHECK(vec1.front().age.value() == 200);
    CHECK(vec1.front().name.value() == "purecpp");
    CHECK(vec1.front().empty_.has_value() == false);
    auto vec2 = postgres.query_s<test_optional>("select * from test_optional;");
    REQUIRE(vec2.size() > 0);
    CHECK(vec2.front().age.value() == 200);
    CHECK(vec2.front().name.value() == "purecpp");
    CHECK(vec2.front().empty_.has_value() == false);
  }
#endif
#ifdef ORMPP_ENABLE_SQLITE3
  dbng<sqlite> sqlite;
#ifdef SQLITE_HAS_CODEC
  if (sqlite.connect(db, password)) {
#else
  if (sqlite.connect(db)) {
#endif
    sqlite.execute("drop table if exists test_optional;");
    sqlite.create_datatable<test_optional>(
        ormpp_auto_key{col_name(&test_optional::id)});
    sqlite.insert<test_optional>({0, "purecpp", 200});
    sqlite.insert<test_optional>({0, "test", 300});
    {
      auto l = sqlite.select_all().from<test_optional>().collect();
      auto l1 =
          sqlite.select(col(&test_optional::id), col(&test_optional::name))
              .from<test_optional>()
              .collect();
      CHECK(l.size() == 2);
      CHECK(l1.size() == 2);
    }
    {
      auto l =
          sqlite.select_all().from<test_optional>().count().collect<uint64_t>();
      auto l2 = sqlite.select_all()
                    .from<test_optional>()
                    .count(col(&test_optional::id))
                    .collect<uint64_t>();
      auto l3 = sqlite.select_all()
                    .from<test_optional>()
                    .count_distinct(col(&test_optional::id))
                    .collect<uint64_t>();
      CHECK(l == 2);
      CHECK(l2 == 2);
      CHECK(l3 == 2);

      auto l4 = sqlite.select_all()
                    .from<test_optional>()
                    .sum(col(&test_optional::id))
                    .collect<uint64_t>();
      auto l5 = sqlite.select_all()
                    .from<test_optional>()
                    .avg(col(&test_optional::id))
                    .collect<uint64_t>();
      auto l6 = sqlite.select_all()
                    .from<test_optional>()
                    .min(col(&test_optional::id))
                    .collect<uint64_t>();
      auto l7 = sqlite.select_all()
                    .from<test_optional>()
                    .max(col(&test_optional::id))
                    .collect<uint64_t>();
      CHECK(l4 == 3);
      CHECK(l5 == 1);
      CHECK(l6 == 1);
      CHECK(l7 == 2);
    }
    auto l1 = sqlite.select_all()
                  .from<test_optional>()
                  .where(col(&test_optional::id).in(1, 2))
                  .order_by(col(&test_optional::id).desc(),
                            col(&test_optional::name).desc())
                  .limit(5)
                  .offset(0)
                  .collect();
    auto ll1 = sqlite.select_all()
                   .from<test_optional>()
                   .where(col(&test_optional::id).not_in(1, 2))
                   .collect();
    auto ll2 = sqlite.select_all()
                   .from<test_optional>()
                   .where(col(&test_optional::id).null())
                   .collect();
    auto ll3 = sqlite.select_all()
                   .from<test_optional>()
                   .where(col(&test_optional::name).not_null())
                   .collect();
    CHECK(ll1.size() == 0);
    CHECK(ll2.size() == 0);
    CHECK(ll3.size() == 2);

    auto l2 = sqlite.select_all()
                  .from<test_optional>()
                  .where(col(&test_optional::name).in("test", "purecpp"))
                  .collect();
    CHECK(l1.size() == 2);
    CHECK(l2.size() == 2);

    auto l3 = sqlite.select_all()
                  .from<test_optional>()
                  .where(col(&test_optional::id).between(1, 2))
                  .collect();

    auto l4 = sqlite.select_all()
                  .from<test_optional>()
                  .where(col(&test_optional::name).between("purecpp", "test"))
                  .collect();
    auto l5 = sqlite.select_all()
                  .from<test_optional>()
                  .where(col(&test_optional::name).like("pure%"))
                  .collect();
    CHECK(l3.size() == 2);
    CHECK(l4.size() == 2);
    CHECK(l5.size() == 1);
    auto list =
        sqlite.select_all()
            .from<test_optional>()
            .where(col(&test_optional::id) == 1 || col(&test_optional::id) == 2)
            .collect();
    REQUIRE(list.size() == 2);
    auto list1 = sqlite.select_all().from<test_optional>().collect();
    REQUIRE(list1.size() == 2);
    auto list2 = sqlite.select_all()
                     .from<test_optional>()
                     .where(col(&test_optional::id) == 2)
                     .collect();
    REQUIRE(list2.size() == 1);
    auto list3 = sqlite.select_all()
                     .from<test_optional>()
                     .where(col(&test_optional::name) == "test")
                     .collect();
    REQUIRE(list3.size() == 1);
    auto vec1 = sqlite.query_s<test_optional>();
    REQUIRE(vec1.size() > 0);
    CHECK(vec1.front().age.value() == 200);
    CHECK(vec1.front().name.value() == "purecpp");
    CHECK(vec1.front().empty_.has_value() == false);
    auto vec2 = sqlite.query_s<test_optional>("select * from test_optional;");
    REQUIRE(vec2.size() > 0);
    CHECK(vec2.front().age.value() == 200);
    CHECK(vec2.front().name.value() == "purecpp");
    CHECK(vec2.front().empty_.has_value() == false);
  }
#endif
}

struct test_order {
  int id;
  std::string name;
};

TEST_CASE("random reflection order") {
#ifdef ORMPP_ENABLE_MYSQL
  dbng<mysql> mysql;
  if (mysql.connect(ip, username, password, db,
                    /*timeout_seconds=*/5, 3306)) {
    mysql.execute("drop table if exists test_order;");
    REQUIRE(mysql.execute(
        "create table if not exists `test_order` (id int, name text);"));
    int id = 666;
    std::string name = "hello";
    mysql.insert(test_order{id, name});
    auto vec = mysql.query_s<test_order>();
    REQUIRE(vec.size() > 0);
    CHECK(vec.front().id == id);
    CHECK(vec.front().name == name);
  }
#endif
}

struct custom_name {
  int id;
  std::string name;
  static constexpr std::string_view get_alias_struct_name(custom_name *) {
    return "test_order";
  }
};

TEST_CASE("custom name") {
#ifdef ORMPP_ENABLE_MYSQL
  dbng<mysql> mysql;
  if (mysql.connect(ip, username, password, db)) {
    auto vec1 = mysql.query_s<custom_name>();
    CHECK(vec1.size() > 0);
    auto vec2 = mysql.query_s<custom_name>("name='hello'");
    CHECK(vec2.size() > 0);
  }
#endif
}

struct dummy {
  int id;
  std::string name;
};

// TEST_CASE("mysql exist tb") {
//   dbng<mysql> mysql;
//   REQUIRE(mysql.connect(ip, username, password, db,
//                         /*timeout_seconds=*/5, 3306));
//   dummy d{0, "tom"};
//   dummy d1{0, "jerry"};
//   mysql.insert(d);
//   mysql.insert(d1);
//   auto v = mysql.query_s<dummy>("limit 1, 1");
//   std::cout << v.size() << "\n";
// }

// #ifdef ORMPP_ENABLE_MYSQL
// TEST_CASE("mysql pool") {
//	dbng<sqlite> sqlite;
//	sqlite.connect(db);
//	sqlite.create_datatable<test_tb>(ormpp_unique{{"name"}});
//	test_tb tb{ 1, "aa" };
//	sqlite.insert(tb);
//	auto vt = sqlite.query_s<test_tb>();
//	auto vt1 = sqlite.query_s<std::tuple<test_tb>>("select * from test_tb");
//    auto& pool = connection_pool<dbng<mysql>>::instance();
//    try {
//        pool.init(1, ip, username, password, db, 2);
//    }catch(const std::exception& e){
//        std::cout<<e.what()<<std::endl;
//        return;
//    }
//	auto con = pool.get();
//	auto v = con->query_s<std::tuple<test_tb>>("select * from test_tb");
//	con->create_datatable<test_tb>(ormpp_unique{{"name"}});
//    for (int i = 0; i < 10; ++i) {
//        auto conn = pool.get();
////        conn_guard guard(conn);
//        if(conn== nullptr){
//            std::cout<<"no available conneciton"<<std::endl;
//            break;
//        }
//
//        bool r = conn->create_datatable<person>();
//    }
// }
// #endif

// #ifdef ORMPP_ENABLE_PG
// TEST_CASE("postgres_pool") {
//   auto &pool = connection_pool<dbng<postgresql>>::instance();
//   try {
//     pool.init(3, ip, username, password, db, /*timeout_seconds=*/5, 5432);
//     pool.init(7, ip, username, password, db, /*timeout_seconds=*/5, 5432);
//   } catch (const std::exception &e) {
//     std::cout << e.what() << std::endl;
//     return;
//   }

//   auto conn1 = pool.get();
//   auto conn2 = pool.get();
//   auto conn3 = pool.get();

//   std::thread thd([conn2, &pool] {
//     std::this_thread::sleep_for(std::chrono::seconds(15));
//     pool.return_back(conn2);
//   });

//   auto conn4 = pool.get();  // 10s later, timeout
//   CHECK(conn4 == nullptr);
//   auto conn5 = pool.get();
//   CHECK(conn5 != nullptr);

//   thd.join();

//   for (int i = 0; i < 10; ++i) {
//     auto conn = pool.get();
//     // conn_guard guard(conn);
//     if (conn == nullptr) {
//       std::cout << "no available conneciton" << std::endl;
//       continue;
//     }

//     bool r = conn->create_datatable<person>();
//   }
// }
// #endif

TEST_CASE("ormpp cfg") {
  ormpp_cfg cfg{};
  bool ret = config_manager::from_file(cfg, "../cfg/ormpp.cfg");
  if (!ret) {
    return;
  }

#ifdef ORMPP_ENABLE_MYSQL
  auto &pool = connection_pool<dbng<mysql>>::instance();
  try {
    cfg.db_port = 3306;
    pool.init(cfg.db_conn_num, cfg.db_ip.data(), cfg.user_name.data(),
              cfg.pwd.data(), cfg.db_name.data(), cfg.timeout, cfg.db_port);
  } catch (const std::exception &e) {
    std::cout << e.what() << std::endl;
    return;
  }

  auto conn1 = pool.get();
  auto vec = conn1->query_s<student>();
  std::cout << vec.size() << std::endl;
#endif
}

TEST_CASE("connect") {
  int timeout = 5;

#ifdef ORMPP_ENABLE_PG
  dbng<postgresql> postgres;
  REQUIRE(postgres.connect(ip, username, password, db));
  REQUIRE(postgres.disconnect());
  REQUIRE(postgres.connect(ip, username, password, db, timeout));
#endif

#ifdef ORMPP_ENABLE_MYSQL
  dbng<mysql> mysql;
  REQUIRE(mysql.connect(ip, username, password, db));
  REQUIRE(mysql.disconnect());
  REQUIRE(mysql.connect(ip, username, password, db, timeout));
#endif

#ifdef ORMPP_ENABLE_SQLITE3
  dbng<sqlite> sqlite;
#ifdef SQLITE_HAS_CODEC
  REQUIRE(sqlite.connect(db, password));
#else
  REQUIRE(sqlite.connect(db));
#endif
  REQUIRE(sqlite.disconnect());
#ifdef SQLITE_HAS_CODEC
  REQUIRE(sqlite.connect(db, password));
#else
  REQUIRE(sqlite.connect(db));
#endif
#endif
}

TEST_CASE("sqlcipher false password connection") {
#if defined(ORMPP_ENABLE_SQLITE3) && defined(SQLITE_HAS_CODEC)
  dbng<sqlite> sqlite;
  // First connect with the correct password
  REQUIRE(sqlite.connect(db, password));

  // Create a test table and insert data
  sqlite.execute("DROP TABLE IF EXISTS person");
  REQUIRE(sqlite.create_datatable<person>(ormpp_auto_key{"id"}));
  REQUIRE(sqlite.insert<person>({"encryption_test", 100}) == 1);

  // Verify data was inserted correctly
  auto results = sqlite.query_s<person>();
  REQUIRE(results.size() == 1);
  CHECK(results[0].name == "encryption_test");

  // Disconnect database
  REQUIRE(sqlite.disconnect());

  // Try to connect with incorrect password
  const char *wrong_password = "wrong_password";
  CHECK(sqlite.connect(db, wrong_password) == false);

  // Connect again with correct password to verify database is still intact
  REQUIRE(sqlite.connect(db, password));
  results = sqlite.query_s<person>();
  REQUIRE(results.size() == 1);
  CHECK(results[0].name == "encryption_test");
#endif
}

TEST_CASE("sqlcipehr with username and password connection") {
#if defined(ORMPP_ENABLE_SQLITE3) && defined(SQLITE_HAS_CODEC)

  dbng<sqlite> sqlite;
  // First connect with the correct password
  REQUIRE(sqlite.connect(db, username, password));

  // Create a test table and insert data
  sqlite.execute("DROP TABLE IF EXISTS person");
  REQUIRE(sqlite.create_datatable<person>(ormpp_auto_key{"id"}));
  REQUIRE(sqlite.insert<person>({"encryption_test", 100}) == 1);

  // Verify data was inserted correctly
  auto results = sqlite.query_s<person>();
  REQUIRE(results.size() == 1);
  CHECK(results[0].name == "encryption_test");

  // Disconnect database
  REQUIRE(sqlite.disconnect());

  // Try to connect with incorrect password
  const char *wrong_password = "wrong_password";
  CHECK(sqlite.connect(db, username, wrong_password) == false);

  // Connect again with correct password to verify database is still intact
  REQUIRE(sqlite.connect(db, username, password));
  results = sqlite.query_s<person>();
  REQUIRE(results.size() == 1);
  CHECK(results[0].name == "encryption_test");
#endif
}

TEST_CASE("create table") {
  ormpp_key key{"id"};
  ormpp_not_null not_null{{"id", "age"}};
  ormpp_auto_key auto_key{"id"};

#ifdef ORMPP_ENABLE_PG
  dbng<postgresql> postgres;
  REQUIRE(postgres.connect(ip, username, password, db));
  REQUIRE(postgres.create_datatable<person>());
  REQUIRE(postgres.create_datatable<person>(key));
  REQUIRE(postgres.create_datatable<person>(not_null));
  REQUIRE(postgres.create_datatable<person>(key, not_null));
  REQUIRE(postgres.create_datatable<person>(not_null, key));
  REQUIRE(postgres.create_datatable<person>(auto_key));
  REQUIRE(postgres.create_datatable<person>(auto_key, not_null));
  REQUIRE(postgres.create_datatable<person>(not_null, auto_key));
#endif

#ifdef ORMPP_ENABLE_SQLITE3
  dbng<sqlite> sqlite;
#ifdef SQLITE_HAS_CODEC
  REQUIRE(sqlite.connect(db, password));
#else
  REQUIRE(sqlite.connect(db));
#endif
  REQUIRE(sqlite.create_datatable<person>());
  REQUIRE(sqlite.create_datatable<person>(key));
  REQUIRE(sqlite.create_datatable<person>(not_null));
  REQUIRE(sqlite.create_datatable<person>(key, not_null));
  REQUIRE(sqlite.create_datatable<person>(not_null, key));
  REQUIRE(sqlite.create_datatable<person>(auto_key));
  REQUIRE(sqlite.create_datatable<person>(auto_key, not_null));
  REQUIRE(sqlite.create_datatable<person>(not_null, auto_key));
#endif

#ifdef ORMPP_ENABLE_MYSQL
  dbng<mysql> mysql;
  REQUIRE(mysql.connect(ip, username, password, db));
  REQUIRE(mysql.create_datatable<person>());
  REQUIRE(mysql.create_datatable<person>(key));
  REQUIRE(mysql.create_datatable<person>(not_null));
  REQUIRE(mysql.create_datatable<person>(key, not_null));
  REQUIRE(mysql.create_datatable<person>(not_null, key));
  REQUIRE(mysql.create_datatable<person>(auto_key));
  REQUIRE(mysql.create_datatable<person>(auto_key, not_null));
  REQUIRE(mysql.create_datatable<person>(not_null, auto_key));
#endif
}

TEST_CASE("insert query") {
  ormpp_key key{col_name(&student::code)};
  ormpp_not_null not_null{{col_name(&student::code), col_name(&student::age)}};
  ormpp_auto_key auto_key{col_name(&student::code)};

  student s = {1, "tom", 0, 19, 1.5, "room2"};
  student s1 = {2, "jack", 1, 20, 2.5, "room3"};
  student s2 = {3, "mke", 2, 21, 3.5, "room4"};
  std::vector<student> v{s1, s2};

#ifdef ORMPP_ENABLE_MYSQL
  dbng<mysql> mysql;
  if (mysql.connect(ip, username, password, db)) {
    mysql.insert(person{"tom", 18});
    auto vec = mysql.query_s<person>("id<5");
    auto vec1 =
        mysql.select_all().from<person>().where(col(&person::id) < 5).collect();
    CHECK(vec.size() == vec1.size());
    CHECK(vec.front().name == vec1.front().name);
  }
#endif

#ifdef ORMPP_ENABLE_PG
  dbng<postgresql> postgres;
  if (postgres.connect(ip, username, password, db)) {
    postgres.insert(person{"tom", 18});
    auto vec = postgres.query_s<person>("id<5");
    auto vec1 = postgres.select_all()
                    .from<person>()
                    .where(col(&person::id) < 5)
                    .collect();
    CHECK(vec.size() == vec1.size());
    CHECK(vec.front().name == vec1.front().name);
  }
#endif

#ifdef ORMPP_ENABLE_SQLITE3
  dbng<sqlite> sqlite;
#ifdef SQLITE_HAS_CODEC
  if (sqlite.connect(db, password)) {
#else
  if (sqlite.connect(db)) {
#endif
    sqlite.insert(person{"tom", 18});
    auto vec = sqlite.query_s<person>("id<5");
    auto vec1 = sqlite.select_all()
                    .from<person>()
                    .where(col(&person::id) < 5)
                    .collect();
    CHECK(vec.size() == vec1.size());
    CHECK(vec.front().name == vec1.front().name);
  }
#endif

  // auto key
  {
#ifdef ORMPP_ENABLE_MYSQL
    mysql.execute("drop table if exists student;");
    mysql.create_datatable<student>(auto_key, not_null);
    CHECK(mysql.insert(s) == 1);
    auto vec1 = mysql.query_s<student>();
    CHECK(vec1.size() == 1);
    CHECK(mysql.insert(v) == 2);
    auto vec2 = mysql.query_s<student>();
    CHECK(vec2.size() == 3);
    auto vec3 = mysql.query_s<student>("code<5");
    CHECK(vec3.size() == 3);
    auto vec4 = mysql.query_s<student>("limit 2");
    CHECK(vec4.size() == 2);
#endif

#ifdef ORMPP_ENABLE_PG
    postgres.execute("drop table if exists student;");
    postgres.create_datatable<student>(auto_key, not_null);
    CHECK(postgres.insert(s) == 1);
    auto vec1 = postgres.query_s<student>();
    CHECK(vec1.size() == 1);
    CHECK(postgres.insert(v) == 2);
    auto vec2 = postgres.query_s<student>();
    CHECK(vec2.size() == 3);
    auto vec3 = postgres.query(FID(student::code), "<", "5");
    CHECK(vec3.size() == 3);
    auto vec4 = postgres.query_s<student>("limit 2");
    CHECK(vec4.size() == 2);
#endif

#ifdef ORMPP_ENABLE_SQLITE3
    sqlite.execute("drop table if exists student;");
    sqlite.create_datatable<student>(auto_key, not_null);
    CHECK(sqlite.insert(s) == 1);
    auto vec1 = sqlite.query_s<student>();
    CHECK(vec1.size() == 1);
    CHECK(sqlite.insert(v) == 2);
    auto vec2 = sqlite.query_s<student>();
    CHECK(vec2.size() == 3);
    auto vec3 = sqlite.query_s<student>("code<5");
    CHECK(vec3.size() == 3);
    auto vec4 = sqlite.query_s<student>("limit 2");
    CHECK(vec4.size() == 2);
#endif
  }

  // key
  {
#ifdef ORMPP_ENABLE_MYSQL
    mysql.execute("drop table if exists student;");
    mysql.create_datatable<student>(key, not_null);
    CHECK(mysql.insert(s) == 1);
    auto vec = mysql.query_s<student>();
    CHECK(vec.size() == 1);
#endif

#ifdef ORMPP_ENABLE_PG
    postgres.execute("drop table if exists student;");
    postgres.create_datatable<student>(key, not_null);
    CHECK(postgres.insert(s) == 1);
    auto vec = postgres.query_s<student>();
    CHECK(vec.size() == 1);
#endif

#ifdef ORMPP_ENABLE_SQLITE3
    sqlite.execute("drop table if exists student;");
    sqlite.create_datatable<student>(key, not_null);
    CHECK(sqlite.insert(s) == 1);
    auto vec = sqlite.query_s<student>();
    CHECK(vec.size() == 1);
#endif
  }
}

TEST_CASE("update replace") {
#ifdef ORMPP_ENABLE_MYSQL
  dbng<mysql> mysql;
  if (mysql.connect(ip, username, password, db)) {
    mysql.execute("drop table if exists person");
    mysql.create_datatable<person>(ormpp_auto_key{"id"});
    mysql.insert<person>({"purecpp", 100});
    auto vec = mysql.query_s<person>();
    CHECK(vec.size() == 1);
    vec.front().name = "update";
    vec.front().age = 200;
    mysql.update(vec.front());
    vec = mysql.query_s<person>();
    CHECK(vec.size() == 1);
    CHECK(vec.front().name == "update");
    CHECK(vec.front().age == 200);
    mysql.update<person>({"purecpp", 100, 1}, "id=1");
    vec = mysql.query_s<person>();
    CHECK(vec.size() == 1);
    CHECK(vec.front().name == "purecpp");
    CHECK(vec.front().age == 100);
    vec.front().name = "update";
    vec.front().age = 200;
    mysql.replace(vec.front());
    vec = mysql.query_s<person>();
    CHECK(vec.size() == 1);
    CHECK(vec.front().name == "update");
    CHECK(vec.front().age == 200);
  }
#endif
#ifdef ORMPP_ENABLE_PG
  dbng<postgresql> postgres;
  if (postgres.connect(ip, username, password, db)) {
    postgres.execute("drop table if exists person");
    postgres.create_datatable<person>(ormpp_auto_key{"id"});
    postgres.insert<person>({"purecpp", 100});
    auto vec = postgres.query_s<person>();
    CHECK(vec.size() == 1);
    vec.front().name = "update";
    vec.front().age = 200;
    postgres.update(vec.front());
    vec = postgres.query_s<person>();
    CHECK(vec.size() == 1);
    CHECK(vec.front().name == "update");
    CHECK(vec.front().age == 200);
    postgres.update<person>({"purecpp", 100, 1}, "id=1");
    vec = postgres.query_s<person>();
    CHECK(vec.size() == 1);
    CHECK(vec.front().name == "purecpp");
    CHECK(vec.front().age == 100);
    vec.front().name = "update";
    vec.front().age = 200;
    postgres.replace(vec.front());
    vec = postgres.query_s<person>();
    CHECK(vec.size() == 1);
    CHECK(vec.front().name == "update");
    CHECK(vec.front().age == 200);
    postgres.replace<person>({"purecpp", 100, 1}, "id");
    vec = postgres.query_s<person>();
    CHECK(vec.size() == 1);
    CHECK(vec.front().name == "purecpp");
    CHECK(vec.front().age == 100);
  }
#endif
#ifdef ORMPP_ENABLE_SQLITE3
  dbng<sqlite> sqlite;
#ifdef SQLITE_HAS_CODEC
  if (sqlite.connect(db, password)) {
#else
  if (sqlite.connect(db)) {
#endif
    sqlite.execute("drop table if exists person");
    sqlite.create_datatable<person>(ormpp_auto_key{"id"});
    sqlite.insert<person>({"purecpp", 100});
    auto vec = sqlite.query_s<person>();
    CHECK(vec.size() == 1);
    vec.front().name = "update";
    vec.front().age = 200;
    sqlite.update(vec.front());
    vec = sqlite.query_s<person>();
    CHECK(vec.size() == 1);
    CHECK(vec.front().name == "update");
    CHECK(vec.front().age == 200);
    sqlite.update<person>({"purecpp", 100, 1}, "id=1");
    vec = sqlite.query_s<person>();
    CHECK(vec.size() == 1);
    CHECK(vec.front().name == "purecpp");
    CHECK(vec.front().age == 100);
    vec.front().name = "update";
    vec.front().age = 200;
    sqlite.replace(vec.front());
    vec = sqlite.query_s<person>();
    CHECK(vec.size() == 1);
    CHECK(vec.front().name == "update");
    CHECK(vec.front().age == 200);
  }
#endif
}

TEST_CASE("update") {
  ormpp_key key{"code"};
  ormpp_not_null not_null{{"code", "age"}};
  ormpp_auto_key auto_key{"code"};

  student s = {1, "tom", 0, 19, 1.5, "room2"};
  student s1 = {2, "jack", 1, 20, 2.5, "room3"};
  student s2 = {3, "mke", 2, 21, 3.5, "room4"};
  std::vector<student> v{s, s1, s2};

#ifdef ORMPP_ENABLE_MYSQL
  dbng<mysql> mysql;
  if (mysql.connect(ip, username, password, db)) {
    mysql.execute("drop table if exists student;");
    mysql.create_datatable<student>(key, not_null);
    CHECK(mysql.insert(v) == 3);
    v[0].name = "test1";
    v[1].name = "test2";
    CHECK(mysql.update(v[0]) == 1);
    auto vec1 = mysql.query_s<student>();
    CHECK(mysql.update(v[1]) == 1);
    auto vec2 = mysql.query_s<student>();
  }
#endif

#ifdef ORMPP_ENABLE_PG
  dbng<postgresql> postgres;
  if (postgres.connect(ip, username, password, db)) {
    postgres.execute("drop table if exists student;");
    postgres.create_datatable<student>(key, not_null);
    CHECK(postgres.insert(v) == 3);
    v[0].name = "test1";
    v[1].name = "test2";
    CHECK(postgres.update(v[0]) == 1);
    auto vec1 = postgres.query_s<student>();
    CHECK(postgres.update(v[1]) == 1);
    auto vec2 = postgres.query_s<student>();
  }
#endif

#ifdef ORMPP_ENABLE_SQLITE3
  dbng<sqlite> sqlite;
#ifdef SQLITE_HAS_CODEC
  if (sqlite.connect(db, password)) {
#else
  if (sqlite.connect(db)) {
#endif
    sqlite.execute("drop table if exists student;");
    sqlite.create_datatable<student>(key);
    CHECK(sqlite.insert(v) == 3);
    v[0].name = "test1";
    v[1].name = "test2";
    CHECK(sqlite.update(v[0]) == 1);
    auto vec1 = sqlite.query_s<student>();
    CHECK(sqlite.update(v[1]) == 1);
    auto vec2 = sqlite.query_s<student>();
  }
#endif
}

TEST_CASE("multi update") {
  ormpp_key key{"code"};
  ormpp_not_null not_null{{"code", "age"}};
  ormpp_auto_key auto_key{"code"};

  student s = {1, "tom", 0, 19, 1.5, "room2"};
  student s1 = {2, "jack", 1, 20, 2.5, "room3"};
  student s2 = {3, "mike", 2, 21, 3.5, "room4"};
  std::vector<student> v{s, s1, s2};

#ifdef ORMPP_ENABLE_MYSQL
  dbng<mysql> mysql;
  if (mysql.connect(ip, username, password, db)) {
    mysql.execute("drop table if exists student");
    mysql.create_datatable<student>(auto_key, not_null);
    CHECK(mysql.insert(v) == 3);
    v[0].name = "test1";
    v[1].name = "test2";
    v[2].name = "test3";
    CHECK(mysql.update(v) == 3);
    auto vec = mysql.query_s<student>();
    CHECK(vec.size() == 3);
  }
#endif

#ifdef ORMPP_ENABLE_PG
  dbng<postgresql> postgres;
  if (postgres.connect(ip, username, password, db)) {
    postgres.execute("drop table if exists student");
    postgres.create_datatable<student>(auto_key, not_null);
    CHECK(postgres.insert(v) == 3);
    v[0].name = "test1";
    v[1].name = "test2";
    v[2].name = "test3";
    CHECK(postgres.update(v) == 3);
    auto vec = postgres.query_s<student>();
    CHECK(vec.size() == 3);
  }
#endif

#ifdef ORMPP_ENABLE_SQLITE3
  dbng<sqlite> sqlite;
#ifdef SQLITE_HAS_CODEC
  if (sqlite.connect(db, password)) {
#else
  if (sqlite.connect(db)) {
#endif
    sqlite.execute("drop table if exists student");
    sqlite.create_datatable<student>(auto_key, not_null);
    CHECK(sqlite.insert(v) == 3);
    v[0].name = "test1";
    v[1].name = "test2";
    v[2].name = "test3";
    CHECK(sqlite.update(v) == 3);
    auto vec = sqlite.query_s<student>();
    CHECK(vec.size() == 3);
  }
#endif
}

TEST_CASE("delete") {
  ormpp_key key{"code"};
  ormpp_not_null not_null{{"code", "age"}};
  ormpp_auto_key auto_key{"code"};

  student s = {1, "tom", 0, 19, 1.5, "room2"};
  student s1 = {2, "jack", 1, 20, 2.5, "room3"};
  student s2 = {3, "mike", 2, 21, 3.5, "room4"};
  std::vector<student> v{s, s1, s2};

#ifdef ORMPP_ENABLE_MYSQL
  dbng<mysql> mysql;
  if (mysql.connect(ip, username, password, db)) {
    mysql.execute("drop table if exists student;");
    mysql.create_datatable<student>(key, not_null);
    CHECK(mysql.insert(v) == 3);
    mysql.delete_records_s<student>("code=1");
    auto vec1 = mysql.query_s<student>();
    CHECK(vec1.size() == 2);
    mysql.delete_records_s<student>("");
    auto vec2 = mysql.query_s<student>();
    CHECK(vec2.size() == 0);
  }
#endif

#ifdef ORMPP_ENABLE_PG
  dbng<postgresql> postgres;
  if (postgres.connect(ip, username, password, db)) {
    postgres.execute("drop table if exists student;");
    postgres.create_datatable<student>(key, not_null);
    CHECK(postgres.insert(v) == 3);
    postgres.delete_records_s<student>("code=1");
    auto vec1 = postgres.query_s<student>();
    CHECK(vec1.size() == 2);
    postgres.delete_records_s<student>("");
    auto vec2 = postgres.query_s<student>();
    CHECK(vec2.size() == 0);
  }
#endif

#ifdef ORMPP_ENABLE_SQLITE3
  dbng<sqlite> sqlite;
#ifdef SQLITE_HAS_CODEC
  if (sqlite.connect(db, password)) {
#else
  if (sqlite.connect(db)) {
#endif
    sqlite.execute("drop table if exists student;");
    sqlite.create_datatable<student>(key);
    CHECK(sqlite.insert(v) == 3);
    REQUIRE(sqlite.delete_records_s<student>("code=1"));
    auto vec1 = sqlite.query_s<student>();
    CHECK(vec1.size() == 2);
    REQUIRE(sqlite.delete_records_s<student>(""));
    auto vec2 = sqlite.query_s<student>();
    CHECK(vec2.size() == 0);
  }
#endif
}

TEST_CASE("query") {
  ormpp_key key{"id"};
  simple s1 = {1, 2.5, 3, {"s1"}};
  simple s2 = {2, 3.5, 4, {"s2"}};
  simple s3 = {3, 4.5, 5, {"s3"}};
  std::vector<simple> v{s1, s2, s3};

#ifdef ORMPP_ENABLE_MYSQL
  dbng<mysql> mysql;
  if (mysql.connect(ip, username, password, db)) {
    mysql.execute("drop table if exists simple");
    mysql.create_datatable<simple>(key);
    CHECK(mysql.insert(v) == 3);
    auto vec1 = mysql.query_s<simple>();
    CHECK(vec1.size() == 3);
    auto vec2 = mysql.query_s<simple>("id=1");
    CHECK(vec2.size() == 1);
  }
#endif

#ifdef ORMPP_ENABLE_PG
  dbng<postgresql> postgres;
  if (postgres.connect(ip, username, password, db)) {
    postgres.execute("drop table if exists simple");
    postgres.create_datatable<simple>(key);
    CHECK(postgres.insert(v) == 3);
    auto vec1 = postgres.query_s<simple>();
    CHECK(vec1.size() == 3);
    auto vec2 = postgres.query_s<simple>("id=2");
    CHECK(vec2.size() == 1);
  }
#endif

#ifdef ORMPP_ENABLE_SQLITE3
  dbng<sqlite> sqlite;
#ifdef SQLITE_HAS_CODEC
  if (sqlite.connect(db, password)) {
#else
  if (sqlite.connect(db)) {
#endif
    sqlite.execute("drop table if exists simple");
    sqlite.create_datatable<simple>(key);
    CHECK(sqlite.insert(v) == 3);
    auto vec1 = sqlite.query_s<simple>();
    CHECK(vec1.size() == 3);
    auto vec2 = sqlite.query_s<simple>("id=3");
    CHECK(vec2.size() == 1);
  }
#endif
}

TEST_CASE("query some") {
  ormpp_key key{"code"};
  ormpp_not_null not_null{{"code", "age"}};
  ormpp_auto_key auto_key{"code"};

  student s = {1, "tom", 0, 19, 1.5, "room2"};
  student s1 = {2, "jack", 1, 20, 2.5, "room3"};
  student s2 = {3, "mike", 2, 21, 3.5, "room4"};
  std::vector<student> v{s, s1, s2};

#ifdef ORMPP_ENABLE_MYSQL
  dbng<mysql> mysql;
  if (mysql.connect(ip, username, password, db)) {
    mysql.execute("drop table if exists student;");
    mysql.create_datatable<student>(key, not_null);
    CHECK(mysql.insert(v) == 3);
    auto vec1 = mysql.query_s<std::tuple<int>>("select count(1) from student");
    CHECK(vec1.size() == 1);
    CHECK(std::get<0>(vec1[0]) == 3);
    auto vec2 = mysql.query_s<std::tuple<int, std::string, double>>(
        "select code, name, dm from student");
    CHECK(vec2.size() == 3);
  }
#endif

#ifdef ORMPP_ENABLE_PG
  dbng<postgresql> postgres;
  if (postgres.connect(ip, username, password, db)) {
    postgres.execute("drop table if exists student;");
    postgres.create_datatable<student>(key, not_null);
    CHECK(postgres.insert(v) == 3);
    auto vec1 =
        postgres.query_s<std::tuple<int>>("select count(1) from student");
    CHECK(vec1.size() == 1);
    CHECK(std::get<0>(vec1[0]) == 3);
    auto vec2 = postgres.query_s<std::tuple<int, std::string, double>>(
        "select code, name, dm from student");
    CHECK(vec2.size() == 3);
  }
#endif

#ifdef ORMPP_ENABLE_SQLITE3
  dbng<sqlite> sqlite;
#ifdef SQLITE_HAS_CODEC
  if (sqlite.connect(db, password)) {
#else
  if (sqlite.connect(db)) {
#endif
    sqlite.execute("drop table if exists student;");
    sqlite.create_datatable<student>(key);
    CHECK(sqlite.insert(v) == 3);
    auto vec1 = sqlite.query_s<std::tuple<int>>("select count(1) from student");
    CHECK(vec1.size() == 1);
    CHECK(std::get<0>(vec1[0]) == 3);
    auto vec2 = sqlite.query_s<std::tuple<int, std::string, double>>(
        "select code, name, dm from student");
    CHECK(vec2.size() == 3);
  }
#endif
}

TEST_CASE("query multi table") {
  ormpp_key key{"code"};
  ormpp_not_null not_null{{"code", "age"}};

  student s = {1, "tom", 0, 19, 1.5, "room2"};
  student s1 = {2, "jack", 1, 20, 2.5, "room3"};
  student s2 = {3, "mike", 2, 21, 3.5, "room4"};
  std::vector<student> v{s, s1, s2};

  ormpp_auto_key key1{"id"};
  ormpp_not_null not_null1{{"name", "age"}};

  person p = {"test1", 2, 1};
  person p1 = {"test2", 3, 2};
  person p2 = {"test3", 4, 3};
  std::vector<person> v1{p, p1, p2};

#ifdef ORMPP_ENABLE_MYSQL
  dbng<mysql> mysql;
  if (mysql.connect(ip, username, password, db)) {
    mysql.execute("drop table if exists student;");
    mysql.execute("drop table if exists person;");
    mysql.create_datatable<student>(key, not_null);
    mysql.create_datatable<person>(key1, not_null1);
    CHECK(mysql.insert(v) == 3);
    CHECK(mysql.insert(v1) == 3);
    auto vec1 = mysql.query_s<std::tuple<person, std::string, int>>(
        "select person.*, student.name, student.age from person, student"s);
    CHECK(vec1.size() == 9);
    auto vec2 = mysql.query_s<std::tuple<person, student>>(
        "select * from person, student"s);
    CHECK(vec2.size() == 9);
  }
#endif

#ifdef ORMPP_ENABLE_PG
  dbng<postgresql> postgres;
  if (postgres.connect(ip, username, password, db)) {
    postgres.execute("drop table if exists student;");
    postgres.execute("drop table if exists person;");
    postgres.create_datatable<student>(key, not_null);
    postgres.create_datatable<person>(key1, not_null1);
    CHECK(postgres.insert(v) == 3);
    CHECK(postgres.insert(v1) == 3);
    auto vec1 = postgres.query_s<std::tuple<int, std::string, double>>(
        "select person.*, student.name, student.age from person, student"s);
    CHECK(vec1.size() == 9);
    auto vec2 = postgres.query_s<std::tuple<person, student>>(
        "select * from person, student"s);
    CHECK(vec2.size() == 9);
  }
#endif

#ifdef ORMPP_ENABLE_SQLITE3
  dbng<sqlite> sqlite;
#ifdef SQLITE_HAS_CODEC
  if (sqlite.connect(db, password)) {
#else
  if (sqlite.connect(db)) {
#endif
    sqlite.execute("drop table if exists student;");
    sqlite.execute("drop table if exists person;");
    sqlite.create_datatable<student>(key, not_null);
    sqlite.create_datatable<person>(key1, not_null1);
    CHECK(sqlite.insert(v) == 3);
    CHECK(sqlite.insert(v1) == 3);
    auto vec1 = sqlite.query_s<std::tuple<int, std::string, double>>(
        "select person.*, student.name, student.age from person, student"s);
    CHECK(vec1.size() == 9);
    auto vec2 = sqlite.query_s<std::tuple<person, student>>(
        "select * from person, student"s);
    CHECK(vec2.size() == 9);
  }
#endif
}

TEST_CASE("transaction") {
  ormpp_key key{"code"};
  ormpp_not_null not_null{{"code", "age"}};

  student s = {1, "tom", 0, 19, 1.5, "room2"};
  student s1 = {2, "jack", 1, 20, 2.5, "room3"};
  student s2 = {3, "mike", 2, 21, 3.5, "room4"};
  std::vector<student> v{s, s1, s2};

#ifdef ORMPP_ENABLE_MYSQL
  dbng<mysql> mysql;
  if (mysql.connect(ip, username, password, db)) {
    mysql.execute("drop table if exists student;");
    mysql.create_datatable<student>(key, not_null);
    mysql.begin();
    for (int i = 0; i < 10; ++i) {
      student st = {i + 1, "tom", 0, 19, 1.5, "room2"};
      if (!mysql.insert(st)) {
        mysql.rollback();
        break;
      }
    }
    mysql.commit();
    auto vec = mysql.query_s<student>();
    CHECK(vec.size() == 10);
    CHECK(mysql.delete_records_s<student>() == 10);
    mysql.set_enable_transaction(false);
    mysql.begin();
    if (!mysql.insert(std::vector<student>{{1, "tom1", 0, 19, 1.5, "room1"},
                                           {2, "tom2", 0, 19, 1.5, "room2"}})) {
      mysql.rollback();
    }
    mysql.commit();
    vec = mysql.query_s<student>();
    CHECK(vec.size() == 2);
  }
#endif

#ifdef ORMPP_ENABLE_PG
  dbng<postgresql> postgres;
  if (postgres.connect(ip, username, password, db)) {
    postgres.execute("drop table if exists student;");
    postgres.create_datatable<student>(key, not_null);
    postgres.begin();
    for (int i = 0; i < 10; ++i) {
      student s = {i + 1, "tom", 0, 19, 1.5, "room2"};
      if (!postgres.insert(s)) {
        postgres.rollback();
        break;
      }
    }
    postgres.commit();
    auto vec = postgres.query_s<student>();
    CHECK(vec.size() == 10);
    CHECK(postgres.delete_records_s<student>() == 10);
    postgres.set_enable_transaction(false);
    postgres.begin();
    if (!postgres.insert(
            std::vector<student>{{1, "tom1", 0, 19, 1.5, "room1"},
                                 {2, "tom2", 0, 19, 1.5, "room2"}})) {
      postgres.rollback();
    }
    postgres.commit();
    vec = postgres.query_s<student>();
    CHECK(vec.size() == 2);
  }
#endif

#ifdef ORMPP_ENABLE_SQLITE3
  dbng<sqlite> sqlite;
#ifdef SQLITE_HAS_CODEC
  if (sqlite.connect(db, password)) {
#else
  if (sqlite.connect(db)) {
#endif
    sqlite.execute("drop table if exists student;");
    sqlite.create_datatable<student>(key);
    sqlite.begin();
    for (int i = 0; i < 10; ++i) {
      student st = {i + 1, "tom", 0, 19, 1.5, "room2"};
      if (!sqlite.insert(st)) {
        sqlite.rollback();
        break;
      }
    }
    sqlite.commit();
    auto vec = sqlite.query_s<student>();
    CHECK(vec.size() == 10);
    CHECK(sqlite.delete_records_s<student>() == 10);
    sqlite.set_enable_transaction(false);
    sqlite.begin();
    if (!sqlite.insert(
            std::vector<student>{{1, "tom1", 0, 19, 1.5, "room1"},
                                 {2, "tom2", 0, 19, 1.5, "room2"}})) {
      sqlite.rollback();
    }
    sqlite.commit();
    vec = sqlite.query_s<student>();
    CHECK(vec.size() == 2);
  }
#endif
}

struct log {
  template <typename... Args>
  bool before(Args... args) {
    std::cout << "log before" << std::endl;
    return true;
  }

  template <typename T, typename... Args>
  bool after(T t, Args... args) {
    std::cout << "log after" << std::endl;
    return true;
  }
};

struct validate {
  template <typename... Args>
  bool before(Args... args) {
    std::cout << "validate before" << std::endl;
    return true;
  }

  template <typename T, typename... Args>
  bool after(T t, Args... args) {
    std::cout << "validate after" << std::endl;
    return true;
  }
};

TEST_CASE("aop") {
  // dbng<mysql> mysql;
  // auto r = mysql.wraper_connect<log, validate>(ip, username, password,
  // db); REQUIRE(r);

  // r = mysql.wraper_execute("drop table if exists person");
  // REQUIRE(r);

  // r = mysql.wraper_execute<log>("drop table if exists person");
  // REQUIRE(r);

  // r = mysql.wraper_execute<validate>("drop table if exists person");
  // REQUIRE(r);

  // r = mysql.wraper_execute<validate, log>("drop table if exists person");
  // REQUIRE(r);
}

struct image {
  int id;
  ormpp::blob bin;
};

TEST_CASE("blob") {
#ifdef ORMPP_ENABLE_MYSQL
  dbng<mysql> mysql;
  if (mysql.connect(ip, username, password, db)) {
    mysql.execute("drop table if exists image");
    mysql.create_datatable<image>();
    auto data = "this is a test binary stream\0, and ?...";
    auto size = 40;
    image img{1};
    img.bin.assign(data, data + size);
    CHECK(mysql.insert(img) == 1);
    auto vec = mysql.query_s<image>("id=?", 1);
    CHECK(vec.size() == 1);
    CHECK(vec.front().bin.size() == size);
  }
#endif
#ifdef ORMPP_ENABLE_PG
  dbng<postgresql> postgres;
  if (postgres.connect(ip, username, password, db)) {
    postgres.execute("drop table if exists image");
    postgres.create_datatable<image>();
    auto data = "this is a test binary stream\0, and ?...";
    auto size = 40;
    image img{1};
    img.bin.assign(data, data + size);
    CHECK(postgres.insert(img) == 1);
    auto vec = postgres.query_s<image>(
        "select id,convert_from(bin,'utf8') from image where id=$1;", 1);
    CHECK(vec.size() == 1);
    // CHECK(vec.front().bin.size() == size);
  }
#endif
#ifdef ORMPP_ENABLE_SQLITE3
  dbng<sqlite> sqlite;
#ifdef SQLITE_HAS_CODEC
  if (sqlite.connect(db, password)) {
#else
  if (sqlite.connect(db)) {
#endif
    sqlite.execute("drop table if exists image");
    sqlite.create_datatable<image>();
    auto data = "this is a test binary stream\0, and ?...";
    auto size = 40;
    image img{1};
    img.bin.assign(data, data + size);
    CHECK(sqlite.insert(img) == 1);
    auto vec = sqlite.query_s<image>("id=?", 1);
    CHECK(vec.size() == 1);
    CHECK(vec.front().bin.size() == size);
  }
#endif
}

struct image_ex {
  int id;
  ormpp::blob bin;
  std::string time;
};

TEST_CASE("blob tuple") {
#ifdef ORMPP_ENABLE_MYSQL
  dbng<mysql> mysql;
  if (mysql.connect(ip, username, password, db)) {
    mysql.execute("drop table if exists image_ex");
    mysql.create_datatable<image_ex>();
    auto data = "this is a test binary stream\0, and ?...";
    auto size = 40;
    image_ex img_ex{1};
    img_ex.bin.assign(data, data + size);
    img_ex.time = "2023-03-29 13:55:00";
    CHECK(mysql.insert(img_ex) == 1);
    auto vec1 = mysql.query_s<image_ex>("id=?", 1);
    CHECK(vec1.size() == 1);
    CHECK(vec1.front().bin.size() == size);
    using image_t = std::tuple<image, std::string>;
    auto vec2 = mysql.query_s<image_t>(
        "select id,bin,time from image_ex where id=?;", 1);
    CHECK(vec2.size() == 1);
    auto &img = std::get<0>(vec2.front());
    auto &time = std::get<1>(vec2.front());
    CHECK(img.id == 1);
    CHECK(time == img_ex.time);
    CHECK(img.bin.size() == size);
  }
#endif
#ifdef ORMPP_ENABLE_PG
  dbng<postgresql> postgres;
  if (postgres.connect(ip, username, password, db)) {
    postgres.execute("drop table if exists image_ex");
    postgres.create_datatable<image_ex>();
    auto data = "this is a test binary stream\0, and ?...";
    auto size = 40;
    image_ex img_ex{1};
    img_ex.bin.assign(data, data + size);
    img_ex.time = "2023-03-29 13:55:00";
    CHECK(postgres.insert(img_ex) == 1);
    auto vec1 = postgres.query_s<image_ex>(
        "select id,convert_from(bin,'utf8'),time from image_ex where id=$1;",
        1);
    CHECK(vec1.size() == 1);
    // CHECK(vec1.front().bin.size() == size);
    using image_t = std::tuple<image, std::string>;
    auto vec2 = postgres.query_s<image_t>(
        "select id,convert_from(bin,'utf8'),time from image_ex where id=$1;",
        1);
    CHECK(vec2.size() == 1);
    auto &img = std::get<0>(vec2.front());
    auto &time = std::get<1>(vec2.front());
    CHECK(img.id == 1);
    CHECK(time == img_ex.time);
    // CHECK(img.bin.size() == size);
  }
#endif
#ifdef ORMPP_ENABLE_SQLITE3
  dbng<sqlite> sqlite;
#ifdef SQLITE_HAS_CODEC
  if (sqlite.connect(db, password)) {
#else
  if (sqlite.connect(db)) {
#endif
    sqlite.execute("drop table if exists image_ex");
    sqlite.create_datatable<image_ex>();
    auto data = "this is a test binary stream\0, and ?...";
    auto size = 40;
    image_ex img_ex{1};
    img_ex.bin.assign(data, data + size);
    img_ex.time = "2023-03-29 13:55:00";
    CHECK(sqlite.insert(img_ex) == 1);
    auto vec1 = sqlite.query_s<image_ex>("id=?", 1);
    CHECK(vec1.size() == 1);
    CHECK(vec1.front().bin.size() == size);
    using image_t = std::tuple<image, std::string>;
    auto vec2 = sqlite.query_s<image_t>(
        "select id,bin,time from image_ex where id=?;", 1);
    CHECK(vec2.size() == 1);
    auto &img = std::get<0>(vec2.front());
    auto &time = std::get<1>(vec2.front());
    CHECK(img.id == 1);
    CHECK(time == img_ex.time);
    CHECK(img.bin.size() == size);
  }
#endif
}

TEST_CASE("create table with unique") {
#ifdef ORMPP_ENABLE_MYSQL
  dbng<mysql> mysql;
  if (mysql.connect(ip, username, password, db)) {
    mysql.execute("drop table if exists person");
    mysql.create_datatable<person>(ormpp_auto_key{"id"}, ormpp_unique{{"age"}});
    mysql.insert<person>({"purecpp"});
    auto vec1 = mysql.query_s<person>("order by id");
    auto vec2 = mysql.query_s<person>("limit 1");
    CHECK(vec1.size() == 1);
    CHECK(vec2.size() == 1);
    mysql.insert<person>({"purecpp"});
    auto vec3 = mysql.query_s<person>();
    CHECK(vec3.size() == 1);
  }
#endif
#ifdef ORMPP_ENABLE_PG
  dbng<postgresql> postgres;
  if (postgres.connect(ip, username, password, db)) {
    postgres.execute("drop table if exists person");
    postgres.create_datatable<person>(ormpp_auto_key{"id"},
                                      ormpp_unique{{"name", "age"}});
    postgres.insert<person>({"purecpp"});
    auto vec1 = postgres.query_s<person>("order by id");
    auto vec2 = postgres.query_s<person>("limit 1");
    CHECK(vec1.size() == 1);
    CHECK(vec2.size() == 1);
    postgres.insert<person>({"purecpp"});
    auto vec3 = postgres.query_s<person>();
    CHECK(vec3.size() == 1);
  }
#endif
#ifdef ORMPP_ENABLE_SQLITE3
  dbng<sqlite> sqlite;
#ifdef SQLITE_HAS_CODEC
  if (sqlite.connect(db, password)) {
#else
  if (sqlite.connect(db)) {
#endif
    sqlite.execute("drop table if exists person");
    sqlite.create_datatable<person>(ormpp_auto_key{"id"},
                                    ormpp_unique{{"name", "age"}});
    sqlite.insert<person>({"purecpp"});
    auto vec1 = sqlite.query_s<person>("order by id");
    auto vec2 = sqlite.query_s<person>("limit 1");
    CHECK(vec1.size() == 1);
    CHECK(vec2.size() == 1);
    sqlite.insert<person>({"purecpp"});
    auto vec3 = sqlite.query_s<person>();
    CHECK(vec3.size() == 1);
  }
#endif
}

TEST_CASE("get insert id after insert") {
#ifdef ORMPP_ENABLE_MYSQL
  dbng<mysql> mysql;
  if (mysql.connect(ip, username, password, db)) {
    mysql.execute("drop table if exists person");
    mysql.create_datatable<person>(ormpp_auto_key{"id"});
    mysql.insert<person>({"purecpp"});
    mysql.insert<person>({"purecpp"});
    auto id = mysql.get_insert_id_after_insert<person>({"purecpp"});
    CHECK(id == 3);
    id = mysql.get_insert_id_after_insert<person>({{"purecpp"}, {"purecpp"}});
    CHECK(id == 5);
  }
#endif
#ifdef ORMPP_ENABLE_PG
  dbng<postgresql> postgres;
  if (postgres.connect(ip, username, password, db)) {
    postgres.execute("drop table if exists person");
    postgres.create_datatable<person>(ormpp_auto_key{"id"});
    postgres.insert<person>({"purecpp"});
    postgres.insert<person>({"purecpp"});
    auto id = postgres.get_insert_id_after_insert<person>({"purecpp"});
    CHECK(id == 3);
    id =
        postgres.get_insert_id_after_insert<person>({{"purecpp"}, {"purecpp"}});
    CHECK(id == 5);
  }
#endif
#ifdef ORMPP_ENABLE_SQLITE3
  dbng<sqlite> sqlite;
#ifdef SQLITE_HAS_CODEC
  if (sqlite.connect(db, password)) {
#else
  if (sqlite.connect(db)) {
#endif
    sqlite.execute("drop table if exists person");
    sqlite.create_datatable<person>(ormpp_auto_key{"id"});
    sqlite.insert<person>({"purecpp"});
    sqlite.insert<person>({"purecpp"});
    auto id = sqlite.get_insert_id_after_insert<person>({"purecpp"});
    CHECK(id == 3);
    id = sqlite.get_insert_id_after_insert<person>({{"purecpp"}, {"purecpp"}});
    CHECK(id == 5);
  }
#endif
}

TEST_CASE("query_s delete_records_s") {
#ifdef ORMPP_ENABLE_MYSQL
  dbng<mysql> mysql;
  if (mysql.connect(ip, username, password, db)) {
    mysql.execute("drop table if exists person");
    mysql.create_datatable<person>(ormpp_auto_key{"id"});
    mysql.insert<person>({"other"});
    mysql.insert<person>({"purecpp", 200});
    auto v_tp1 = mysql.query_s<std::tuple<person>>(
        "select * from person where name=?", "purecpp");
    auto v_tp2 = mysql.query_s<std::tuple<person>>(
        "select * from person where name=?", "purecpp' or '1=1");
    auto v_tp3 = mysql.query_s<std::tuple<person>>("select * from person");
    auto vec1 = mysql.query_s<person>();
    auto vec2 = mysql.query_s<person>("name=?", "purecpp");
    auto vec3 = mysql.query_s<person>("select * from person");
    auto vec4 = mysql.query_s<person>("name=?", std::string("purecpp"));
    auto vec5 = mysql.query_s<person>("name=? and age=?", "purecpp", 200);
    auto vec6 =
        mysql.query_s<person>("select * from person where name=?", "purecpp");
    auto vec7 = mysql.query_s<person>("name=?", "purecpp' or '1=1");
    CHECK(mysql.delete_records_s<person>("name=?", "purecpp' or '1=1") == 0);
    auto vec8 = mysql.query_s<person>();
    CHECK(mysql.delete_records_s<person>("name=?", "purecpp") == 1);
    auto vec9 = mysql.query_s<person>();
    CHECK(mysql.delete_records_s<person>() == 1);
    auto vec10 = mysql.query_s<person>();
    CHECK(vec1.front().name == "other");
    CHECK(vec1.back().name == "purecpp");
    CHECK(vec3.front().name == "other");
    CHECK(vec3.back().name == "purecpp");
    CHECK(std::get<0>(v_tp3.front()).name == "other");
    CHECK(std::get<0>(v_tp3.back()).name == "purecpp");
    CHECK(vec2.front().age == 200);
    CHECK(vec4.front().age == 200);
    CHECK(vec5.front().age == 200);
    CHECK(vec6.front().age == 200);
    CHECK(std::get<0>(v_tp1.front()).age == 200);
    CHECK(v_tp2.size() == 0);
    CHECK(vec7.size() == 0);
    CHECK(vec8.size() == 2);
    CHECK(vec9.size() == 1);
    CHECK(vec10.size() == 0);
  }
#endif
#ifdef ORMPP_ENABLE_PG
  dbng<postgresql> postgres;
  if (postgres.connect(ip, username, password, db)) {
    postgres.execute("drop table if exists person");
    postgres.create_datatable<person>(ormpp_auto_key{"id"});
    postgres.insert<person>({"other"});
    postgres.insert<person>({"purecpp", 200});
    auto v_tp1 = postgres.query_s<std::tuple<person>>(
        "select * from person where name=$1", "purecpp");
    auto v_tp2 = postgres.query_s<std::tuple<person>>(
        "select * from person where name=$1", "purecpp' or '1=1");
    auto v_tp3 = postgres.query_s<std::tuple<person>>("select * from person");
    auto vec1 = postgres.query_s<person>();
    auto vec2 = postgres.query_s<person>("name=$1", "purecpp");
    auto vec3 = postgres.query_s<person>("select * from person");
    auto vec4 = postgres.query_s<person>("name=$1", std::string("purecpp"));
    auto vec5 = postgres.query_s<person>("name=$1 and age=$2", "purecpp", 200);
    auto vec6 = postgres.query_s<person>("select * from person where name=$1",
                                         "purecpp");
    auto vec7 = postgres.query_s<person>("name=$1", "purecpp' or '1=1");
    CHECK(postgres.delete_records_s<person>("name=$1", "purecpp' or '1=1") ==
          0);
    auto vec8 = postgres.query_s<person>();
    CHECK(postgres.delete_records_s<person>("name=$1", "purecpp") == 1);
    auto vec9 = postgres.query_s<person>();
    CHECK(postgres.delete_records_s<person>() == 1);
    auto vec10 = postgres.query_s<person>();
    CHECK(vec1.front().name == "other");
    CHECK(vec1.back().name == "purecpp");
    CHECK(vec3.front().name == "other");
    CHECK(vec3.back().name == "purecpp");
    CHECK(std::get<0>(v_tp3.front()).name == "other");
    CHECK(std::get<0>(v_tp3.back()).name == "purecpp");
    CHECK(vec2.front().age == 200);
    CHECK(vec4.front().age == 200);
    CHECK(vec5.front().age == 200);
    CHECK(vec6.front().age == 200);
    CHECK(std::get<0>(v_tp1.front()).age == 200);
    CHECK(v_tp2.size() == 0);
    CHECK(vec7.size() == 0);
    CHECK(vec8.size() == 2);
    CHECK(vec9.size() == 1);
    CHECK(vec10.size() == 0);
  }
#endif
#ifdef ORMPP_ENABLE_SQLITE3
  dbng<sqlite> sqlite;
#ifdef SQLITE_HAS_CODEC
  if (sqlite.connect(db, password)) {
#else
  if (sqlite.connect(db)) {
#endif
    sqlite.execute("drop table if exists person");
    sqlite.create_datatable<person>(ormpp_auto_key{"id"});
    sqlite.insert<person>({"other"});
    sqlite.insert<person>({"purecpp", 200});
    auto v_tp1 = sqlite.query_s<std::tuple<person>>(
        "select * from person where name=?", "purecpp");
    auto v_tp2 = sqlite.query_s<std::tuple<person>>(
        "select * from person where name=?", "purecpp' or '1=1");
    auto v_tp3 = sqlite.query_s<std::tuple<person>>("select * from person");
    auto vec1 = sqlite.query_s<person>();
    auto vec2 = sqlite.query_s<person>("name=?", "purecpp");
    auto vec3 = sqlite.query_s<person>("select * from person");
    auto vec4 = sqlite.query_s<person>("name=?", std::string("purecpp"));
    auto vec5 = sqlite.query_s<person>("name=? and age=?", "purecpp", 200);
    auto vec6 =
        sqlite.query_s<person>("select * from person where name=?", "purecpp");
    auto vec11 =
        sqlite.query_s<person>("SELECT * FROM PERSON WHERE NAME=?", "purecpp");
    auto vec7 = sqlite.query_s<person>("name=?", "purecpp' or '1=1");
    CHECK(sqlite.delete_records_s<person>("name=?", "purecpp' or '1=1") == 0);
    auto vec8 = sqlite.query_s<person>();
    CHECK(sqlite.delete_records_s<person>("name=?", "purecpp") == 1);
    auto vec9 = sqlite.query_s<person>();
    CHECK(sqlite.delete_records_s<person>() == 1);
    auto vec10 = sqlite.query_s<person>();
    CHECK(vec1.front().name == "other");
    CHECK(vec1.back().name == "purecpp");
    CHECK(vec3.front().name == "other");
    CHECK(vec3.back().name == "purecpp");
    CHECK(std::get<0>(v_tp3.front()).name == "other");
    CHECK(std::get<0>(v_tp3.back()).name == "purecpp");
    CHECK(vec2.front().age == 200);
    CHECK(vec4.front().age == 200);
    CHECK(vec5.front().age == 200);
    CHECK(vec6.front().age == 200);
    CHECK(std::get<0>(v_tp1.front()).age == 200);
    CHECK(v_tp2.size() == 0);
    CHECK(vec7.size() == 0);
    CHECK(vec8.size() == 2);
    CHECK(vec9.size() == 1);
    CHECK(vec10.size() == 0);
    CHECK(vec11.front().age == 200);
  }
#endif
}

struct tuple_optional_t {
  int id;
  std::optional<std::string> name;
  std::optional<int> age;
};
REGISTER_AUTO_KEY(tuple_optional_t, id)

TEST_CASE("query tuple_optional_t") {
#ifdef ORMPP_ENABLE_MYSQL
  dbng<mysql> mysql;
  if (mysql.connect(ip, username, password, db)) {
    mysql.execute("drop table if exists tuple_optional_t");
    mysql.create_datatable<tuple_optional_t>(ormpp_auto_key{"id"});
    mysql.insert<tuple_optional_t>({0, "purecpp", 6});
    mysql.insert<tuple_optional_t>({0, std::nullopt});
    auto vec =
        mysql.query_s<std::tuple<tuple_optional_t, std::optional<std::string>,
                                 std::optional<int>>>(
            "select id,name,age,name,age from tuple_optional_t;");
    CHECK(vec.size() == 2);
    auto tp1 = vec.front();
    auto tp2 = vec.back();
    auto p1 = std::get<0>(tp1);
    auto p2 = std::get<0>(tp2);
    auto n1 = std::get<1>(tp1);
    auto n2 = std::get<1>(tp2);
    auto a1 = std::get<2>(tp1);
    auto a2 = std::get<2>(tp2);
    CHECK(p1.name.value() == "purecpp");
    CHECK(p1.age.value() == 6);
    CHECK(p2.name.has_value() == false);
    CHECK(p2.age.has_value() == false);
    CHECK(n1.value() == "purecpp");
    CHECK(n2.has_value() == false);
    CHECK(a1.value() == 6);
    CHECK(a2.has_value() == false);
  }
#endif
#ifdef ORMPP_ENABLE_PG
  dbng<postgresql> postgres;
  if (postgres.connect(ip, username, password, db)) {
    postgres.execute("drop table if exists tuple_optional_t");
    postgres.create_datatable<tuple_optional_t>(ormpp_auto_key{"id"});
    postgres.insert<tuple_optional_t>({0, "purecpp", 6});
    postgres.insert<tuple_optional_t>({0, std::nullopt});
    auto vec = postgres.query_s<std::tuple<
        tuple_optional_t, std::optional<std::string>, std::optional<int>>>(
        "select id,name,age,name,age from tuple_optional_t;");
    CHECK(vec.size() == 2);
    auto tp1 = vec.front();
    auto tp2 = vec.back();
    auto p1 = std::get<0>(tp1);
    auto p2 = std::get<0>(tp2);
    auto n1 = std::get<1>(tp1);
    auto n2 = std::get<1>(tp2);
    auto a1 = std::get<2>(tp1);
    auto a2 = std::get<2>(tp2);
    CHECK(p1.name.value() == "purecpp");
    CHECK(p1.age.value() == 6);
    CHECK(p2.name.has_value() == false);
    CHECK(p2.age.has_value() == false);
    CHECK(n1.value() == "purecpp");
    CHECK(n2.has_value() == false);
    CHECK(a1.value() == 6);
    CHECK(a2.has_value() == false);
  }
#endif
#ifdef ORMPP_ENABLE_SQLITE3
  dbng<sqlite> sqlite;
#ifdef SQLITE_HAS_CODEC
  if (sqlite.connect(db, password)) {
#else
  if (sqlite.connect(db)) {
#endif
    sqlite.execute("drop table if exists tuple_optional_t");
    sqlite.create_datatable<tuple_optional_t>(ormpp_auto_key{"id"});
    sqlite.insert(tuple_optional_t{0, "purecpp", 6});
    sqlite.insert<tuple_optional_t>({0, std::nullopt});
    auto vec =
        sqlite.query_s<std::tuple<tuple_optional_t, std::optional<std::string>,
                                  std::optional<int>>>(
            "select id,name,age,name,age from tuple_optional_t;");
    CHECK(vec.size() == 2);
    auto tp1 = vec.front();
    auto tp2 = vec.back();
    auto p1 = std::get<0>(tp1);
    auto p2 = std::get<0>(tp2);
    auto n1 = std::get<1>(tp1);
    auto n2 = std::get<1>(tp2);
    auto a1 = std::get<2>(tp1);
    auto a2 = std::get<2>(tp2);
    CHECK(p1.name.value() == "purecpp");
    CHECK(p1.age.value() == 6);
    CHECK(p2.name.has_value() == false);
    CHECK(p2.age.has_value() == false);
    CHECK(n1.value() == "purecpp");
    CHECK(n2.has_value() == false);
    CHECK(a1.value() == 6);
    CHECK(a2.has_value() == false);
  }
#endif
}

enum class Color { BLUE = 10, RED = 15 };
enum Fruit { APPLE, BANANA };

struct test_enum_t {
  Color color;
  Fruit fruit;
  int id;
};
REGISTER_AUTO_KEY(test_enum_t, id)

TEST_CASE("test enum") {
#ifdef ORMPP_ENABLE_MYSQL
  dbng<mysql> mysql;
  if (mysql.connect(ip, username, password, db)) {
    mysql.execute("drop table if exists test_enum_t");
    mysql.create_datatable<test_enum_t>(ormpp_auto_key{"id"});
    mysql.insert<test_enum_t>({Color::BLUE});
    auto vec = mysql.query_s<test_enum_t>();
    CHECK(vec.size() == 1);
    CHECK(vec.front().color == Color::BLUE);
    CHECK(vec.front().fruit == APPLE);
    vec.front().color = Color::RED;
    vec.front().fruit = BANANA;
    mysql.update(vec.front());
    vec = mysql.query_s<test_enum_t>();
    CHECK(vec.size() == 1);
    CHECK(vec.front().color == Color::RED);
    CHECK(vec.front().fruit == BANANA);
    mysql.update<test_enum_t>({Color::BLUE, APPLE, 1}, "id=1");
    vec = mysql.query_s<test_enum_t>();
    CHECK(vec.size() == 1);
    CHECK(vec.front().color == Color::BLUE);
    CHECK(vec.front().fruit == APPLE);
    vec.front().color = Color::RED;
    vec.front().fruit = BANANA;
    mysql.replace(vec.front());
    vec = mysql.query_s<test_enum_t>();
    CHECK(vec.size() == 1);
    CHECK(vec.front().color == Color::RED);
    CHECK(vec.front().fruit == BANANA);
    mysql.delete_records_s<test_enum_t>();
    vec = mysql.query_s<test_enum_t>();
    CHECK(vec.size() == 0);
  }
#endif
#ifdef ORMPP_ENABLE_PG
  dbng<postgresql> postgres;
  if (postgres.connect(ip, username, password, db)) {
    postgres.execute("drop table if exists test_enum_t");
    postgres.create_datatable<test_enum_t>(ormpp_auto_key{"id"});
    postgres.insert<test_enum_t>({Color::BLUE});
    auto vec = postgres.query_s<test_enum_t>();
    CHECK(vec.size() == 1);
    CHECK(vec.front().color == Color::BLUE);
    CHECK(vec.front().fruit == APPLE);
    vec.front().color = Color::RED;
    vec.front().fruit = BANANA;
    postgres.update(vec.front());
    vec = postgres.query_s<test_enum_t>();
    CHECK(vec.size() == 1);
    CHECK(vec.front().color == Color::RED);
    CHECK(vec.front().fruit == BANANA);
    postgres.update<test_enum_t>({Color::BLUE, APPLE, 1}, "id=1");
    vec = postgres.query_s<test_enum_t>();
    CHECK(vec.size() == 1);
    CHECK(vec.front().color == Color::BLUE);
    CHECK(vec.front().fruit == APPLE);
    vec.front().color = Color::RED;
    vec.front().fruit = BANANA;
    postgres.replace(vec.front());
    vec = postgres.query_s<test_enum_t>();
    CHECK(vec.size() == 1);
    CHECK(vec.front().color == Color::RED);
    CHECK(vec.front().fruit == BANANA);
    postgres.delete_records_s<test_enum_t>();
    vec = postgres.query_s<test_enum_t>();
    CHECK(vec.size() == 0);
  }
#endif
#ifdef ORMPP_ENABLE_SQLITE3
  dbng<sqlite> sqlite;
#ifdef SQLITE_HAS_CODEC
  if (sqlite.connect(db, password)) {
#else
  if (sqlite.connect(db)) {
#endif
    sqlite.execute("drop table if exists test_enum_t");
    sqlite.create_datatable<test_enum_t>(ormpp_auto_key{"id"});
    sqlite.insert<test_enum_t>({Color::BLUE});
    auto vec = sqlite.query_s<test_enum_t>();
    CHECK(vec.size() == 1);
    CHECK(vec.front().color == Color::BLUE);
    CHECK(vec.front().fruit == APPLE);
    vec.front().color = Color::RED;
    vec.front().fruit = BANANA;
    sqlite.update(vec.front());
    vec = sqlite.query_s<test_enum_t>();
    CHECK(vec.size() == 1);
    CHECK(vec.front().color == Color::RED);
    CHECK(vec.front().fruit == BANANA);
    sqlite.update<test_enum_t>({Color::BLUE, APPLE, 1}, "id=1");
    vec = sqlite.query_s<test_enum_t>();
    CHECK(vec.size() == 1);
    CHECK(vec.front().color == Color::BLUE);
    CHECK(vec.front().fruit == APPLE);
    vec.front().color = Color::RED;
    vec.front().fruit = BANANA;
    sqlite.replace(vec.front());
    vec = sqlite.query_s<test_enum_t>();
    CHECK(vec.size() == 1);
    CHECK(vec.front().color == Color::RED);
    CHECK(vec.front().fruit == BANANA);
    sqlite.delete_records_s<test_enum_t>();
    vec = sqlite.query_s<test_enum_t>();
    CHECK(vec.size() == 0);
  }
#endif
}

struct test_enum_with_name_t {
  int id;
  Color color;
  Fruit fruit;
  static constexpr std::string_view get_alias_struct_name(
      test_enum_with_name_t *) {
    return "test_enum";
  }
};
REGISTER_AUTO_KEY(test_enum_with_name_t, id)

TEST_CASE("test enum with custom name") {
#ifdef ORMPP_ENABLE_MYSQL
  dbng<mysql> mysql;
  if (mysql.connect(ip, username, password, db)) {
    mysql.execute("drop table if exists test_enum");
    mysql.create_datatable<test_enum_with_name_t>(ormpp_auto_key{"id"});
    mysql.insert<test_enum_with_name_t>({0, Color::BLUE});
    auto vec = mysql.query_s<test_enum_with_name_t>();
    CHECK(vec.size() == 1);
    CHECK(vec.front().color == Color::BLUE);
    CHECK(vec.front().fruit == APPLE);
    vec.front().color = Color::RED;
    vec.front().fruit = BANANA;
    mysql.update(vec.front());
    vec = mysql.query_s<test_enum_with_name_t>();
    CHECK(vec.size() == 1);
    CHECK(vec.front().color == Color::RED);
    CHECK(vec.front().fruit == BANANA);
    mysql.update<test_enum_with_name_t>({1, Color::BLUE, APPLE});
    vec = mysql.query_s<test_enum_with_name_t>();
    CHECK(vec.size() == 1);
    CHECK(vec.front().color == Color::BLUE);
    CHECK(vec.front().fruit == APPLE);
    vec.front().color = Color::RED;
    vec.front().fruit = BANANA;
    mysql.replace(vec.front());
    vec = mysql.query_s<test_enum_with_name_t>();
    CHECK(vec.size() == 1);
    CHECK(vec.front().color == Color::RED);
    CHECK(vec.front().fruit == BANANA);
    mysql.delete_records_s<test_enum_with_name_t>();
    vec = mysql.query_s<test_enum_with_name_t>();
    CHECK(vec.size() == 0);
  }
#endif
#ifdef ORMPP_ENABLE_PG
  dbng<postgresql> postgres;
  if (postgres.connect(ip, username, password, db)) {
    postgres.execute("drop table if exists test_enum");
    postgres.create_datatable<test_enum_with_name_t>(ormpp_auto_key{"id"});
    postgres.insert<test_enum_with_name_t>({0, Color::BLUE});
    auto vec = postgres.query_s<test_enum_with_name_t>();
    CHECK(vec.size() == 1);
    CHECK(vec.front().color == Color::BLUE);
    CHECK(vec.front().fruit == APPLE);
    vec.front().color = Color::RED;
    vec.front().fruit = BANANA;
    postgres.update(vec.front());
    vec = postgres.query_s<test_enum_with_name_t>();
    CHECK(vec.size() == 1);
    CHECK(vec.front().color == Color::RED);
    CHECK(vec.front().fruit == BANANA);
    postgres.update<test_enum_with_name_t>({1, Color::BLUE, APPLE});
    vec = postgres.query_s<test_enum_with_name_t>();
    CHECK(vec.size() == 1);
    CHECK(vec.front().color == Color::BLUE);
    CHECK(vec.front().fruit == APPLE);
    vec.front().color = Color::RED;
    vec.front().fruit = BANANA;
    postgres.replace(vec.front());
    vec = postgres.query_s<test_enum_with_name_t>();
    CHECK(vec.size() == 1);
    CHECK(vec.front().color == Color::RED);
    CHECK(vec.front().fruit == BANANA);
    postgres.delete_records_s<test_enum_with_name_t>();
    vec = postgres.query_s<test_enum_with_name_t>();
    CHECK(vec.size() == 0);
  }
#endif
#ifdef ORMPP_ENABLE_SQLITE3
  dbng<sqlite> sqlite;
#ifdef SQLITE_HAS_CODEC
  if (sqlite.connect(db, password)) {
#else
  if (sqlite.connect(db)) {
#endif
    sqlite.execute("drop table if exists test_enum");
    sqlite.create_datatable<test_enum_with_name_t>(ormpp_auto_key{"id"});
    sqlite.insert<test_enum_with_name_t>({0, Color::BLUE});
    auto vec = sqlite.query_s<test_enum_with_name_t>();
    CHECK(vec.size() == 1);
    CHECK(vec.front().color == Color::BLUE);
    CHECK(vec.front().fruit == APPLE);
    vec.front().color = Color::RED;
    vec.front().fruit = BANANA;
    sqlite.update(vec.front());
    vec = sqlite.query_s<test_enum_with_name_t>();
    CHECK(vec.size() == 1);
    CHECK(vec.front().color == Color::RED);
    CHECK(vec.front().fruit == BANANA);
    sqlite.update<test_enum_with_name_t>({1, Color::BLUE, APPLE});
    vec = sqlite.query_s<test_enum_with_name_t>();
    CHECK(vec.size() == 1);
    CHECK(vec.front().color == Color::BLUE);
    CHECK(vec.front().fruit == APPLE);
    vec.front().color = Color::RED;
    vec.front().fruit = BANANA;
    sqlite.replace(vec.front());
    vec = sqlite.query_s<test_enum_with_name_t>();
    CHECK(vec.size() == 1);
    CHECK(vec.front().color == Color::RED);
    CHECK(vec.front().fruit == BANANA);
    sqlite.delete_records_s<test_enum_with_name_t>();
    vec = sqlite.query_s<test_enum_with_name_t>();
    CHECK(vec.size() == 0);
  }
#endif
}

struct test_bool_t {
  bool ok;
  int id;
};
REGISTER_AUTO_KEY(test_bool_t, id)

TEST_CASE("test bool") {
#ifdef ORMPP_ENABLE_MYSQL
  dbng<mysql> mysql;
  if (mysql.connect(ip, username, password, db)) {
    mysql.execute("drop table if exists test_bool_t");
    mysql.create_datatable<test_bool_t>(ormpp_auto_key{"id"});
    mysql.insert(test_bool_t{true});
    auto vec = mysql.query_s<test_bool_t>();
    CHECK(vec.size() == 1);
    CHECK(vec.front().ok == true);
    mysql.delete_records_s<test_bool_t>();
    mysql.insert(test_bool_t{false});
    vec = mysql.query_s<test_bool_t>();
    CHECK(vec.size() == 1);
    CHECK(vec.front().ok == false);
  }
#endif
#ifdef ORMPP_ENABLE_PG
  dbng<postgresql> postgres;
  if (postgres.connect(ip, username, password, db)) {
    postgres.execute("drop table if exists test_bool_t");
    postgres.create_datatable<test_bool_t>(ormpp_auto_key{"id"});
    postgres.insert(test_bool_t{true});
    auto vec = postgres.query_s<test_bool_t>();
    CHECK(vec.size() == 1);
    CHECK(vec.front().ok == true);
    postgres.delete_records_s<test_bool_t>();
    postgres.insert(test_bool_t{false});
    vec = postgres.query_s<test_bool_t>();
    CHECK(vec.size() == 1);
    CHECK(vec.front().ok == false);
  }
#endif
#ifdef ORMPP_ENABLE_SQLITE3
  dbng<sqlite> sqlite;
#ifdef SQLITE_HAS_CODEC
  if (sqlite.connect(db, password)) {
#else
  if (sqlite.connect(db)) {
#endif
    sqlite.execute("drop table if exists test_bool_t");
    sqlite.create_datatable<test_bool_t>(ormpp_auto_key{"id"});
    sqlite.insert(test_bool_t{true});
    auto vec = sqlite.query_s<test_bool_t>();
    CHECK(vec.size() == 1);
    CHECK(vec.front().ok == true);
    sqlite.delete_records_s<test_bool_t>();
    sqlite.insert(test_bool_t{false});
    vec = sqlite.query_s<test_bool_t>();
    CHECK(vec.size() == 1);
    CHECK(vec.front().ok == false);
  }
#endif
}

struct alias {
  int id;
  std::string name;
  static constexpr auto get_alias_field_names(alias *) {
    return std::array{ylt::reflection::field_alias_t{"alias_id", 0},
                      ylt::reflection::field_alias_t{"alias_name", 1}};
  }
  static constexpr std::string_view get_alias_struct_name(alias *) {
    return "t_alias";
  }
};

TEST_CASE("alias") {
#ifdef ORMPP_ENABLE_MYSQL
  dbng<mysql> mysql;
  if (mysql.connect(ip, username, password, db)) {
    mysql.execute("drop table if exists t_alias;");
    mysql.create_datatable<alias>(ormpp_auto_key{"alias_id"});
    mysql.insert<alias>({0, "purecpp"});
    auto vec = mysql.query_s<alias>();
    CHECK(vec.front().name == "purecpp");
  }
#endif
#ifdef ORMPP_ENABLE_PG
  dbng<postgresql> postgres;
  if (postgres.connect(ip, username, password, db)) {
    postgres.execute("drop table if exists t_alias;");
    postgres.create_datatable<alias>(ormpp_auto_key{"alias_id"});
    postgres.insert<alias>({0, "purecpp"});
    auto vec = postgres.query_s<alias>();
    CHECK(vec.front().name == "purecpp");
  }
#endif
#ifdef ORMPP_ENABLE_SQLITE3
  dbng<sqlite> sqlite;
#ifdef SQLITE_HAS_CODEC
  if (sqlite.connect(db, password)) {
#else
  if (sqlite.connect(db)) {
#endif
    sqlite.execute("drop table if exists t_alias;");
    sqlite.create_datatable<alias>(ormpp_auto_key{"alias_id"});
    alias al{.name = "purecpp"};
    sqlite.insert(al);
    sqlite.insert(al);
    auto vec = sqlite.query_s<alias>();
    CHECK(vec.front().name == "purecpp");
  }
#endif
}

#ifdef ORMPP_ENABLE_PG
TEST_CASE("pg update") {
  dbng<postgresql> postgres;
  if (postgres.connect(ip, username, password, db)) {
    postgres.execute("drop table if exists person");
    postgres.create_datatable<person>(ormpp_auto_key{"id"});
    postgres.insert<person>({"purecpp"});
    auto vec1 = postgres.query_s<person>();
    CHECK(vec1.size() == 1);
    vec1.front().name = "other";
    postgres.update<person>(vec1.front());
    auto vec2 = postgres.query_s<person>();
    CHECK(vec2.size() == 1);
    CHECK(vec2.front().name == "other");
    CHECK(vec1.front().id == vec2.front().id);
  }
}
#endif

#if 0
TEST_CASE("update section filed") {
#ifdef ORMPP_ENABLE_MYSQL
  dbng<mysql> mysql;
  if (mysql.connect(ip, username, password, db)) {
    mysql.execute("drop table if exists person");
    mysql.create_datatable<person>(ormpp_auto_key{"id"});
    mysql.insert<person>({"person_a", 1});
    mysql.insert<person>({"person_b", 2});
    mysql.update_some<&person::name>(person{"purecpp_a", 0, 1});
    mysql.update_some<&person::name>(person{"purecpp_b"}, "id=2");
    auto vec1 = mysql.query_s<person>("id=?", 1);
    auto vec2 = mysql.query_s<person>("id=?", 2);
    CHECK(vec1.size() == 1);
    CHECK(vec2.size() == 1);
    CHECK(vec1.front().name == "purecpp_a");
    CHECK(vec2.front().name == "purecpp_b");
    mysql.update_some<&person::age, &person::name>(
        person{"purecpp_aa", 111, 1});
    mysql.update_some<&person::age, &person::name>(person{"purecpp_bb", 222},
                                                   "id=2");
    vec1 = mysql.query_s<person>("id=?", 1);
    vec2 = mysql.query_s<person>("id=?", 2);
    CHECK(vec1.size() == 1);
    CHECK(vec2.size() == 1);
    CHECK(vec1.front().age == 111);
    CHECK(vec2.front().age == 222);
    CHECK(vec1.front().name == "purecpp_aa");
    CHECK(vec2.front().name == "purecpp_bb");
    mysql.update_some<&person::name, &person::age>(
        person{"purecpp_aaa", 333, 1});
    mysql.update_some<&person::name, &person::age>(person{"purecpp_bbb", 444},
                                                   "id=2");
    vec1 = mysql.query_s<person>("id=?", 1);
    vec2 = mysql.query_s<person>("id=?", 2);
    CHECK(vec1.size() == 1);
    CHECK(vec2.size() == 1);
    CHECK(vec1.front().age == 333);
    CHECK(vec2.front().age == 444);
    CHECK(vec1.front().name == "purecpp_aaa");
    CHECK(vec2.front().name == "purecpp_bbb");
    mysql.update_some<&person::name, &person::age>(std::vector<person>{
        {"purecpp_aaaa", 555, 1}, {"purecpp_bbbb", 666, 2}});
    vec1 = mysql.query_s<person>("id=?", 1);
    vec2 = mysql.query_s<person>("id=?", 2);
    CHECK(vec1.size() == 1);
    CHECK(vec2.size() == 1);
    CHECK(vec1.front().age == 555);
    CHECK(vec2.front().age == 666);
    CHECK(vec1.front().name == "purecpp_aaaa");
    CHECK(vec2.front().name == "purecpp_bbbb");
  }
#endif
#ifdef ORMPP_ENABLE_PG
  dbng<postgresql> postgres;
  if (postgres.connect(ip, username, password, db)) {
    postgres.execute("drop table if exists person");
    postgres.create_datatable<person>(ormpp_auto_key{"id"});
    postgres.insert<person>({"person_a", 1});
    postgres.insert<person>({"person_b", 2});
    postgres.update_some<&person::name>(person{"purecpp_a", 0, 1});
    postgres.update_some<&person::name>(person{"purecpp_b"}, "id=2");
    auto vec1 = postgres.query_s<person>("id=$1", 1);
    auto vec2 = postgres.query_s<person>("id=$1", 2);
    CHECK(vec1.size() == 1);
    CHECK(vec2.size() == 1);
    CHECK(vec1.front().name == "purecpp_a");
    CHECK(vec2.front().name == "purecpp_b");
    postgres.update_some<&person::age, &person::name>(
        person{"purecpp_aa", 111, 1});
    postgres.update_some<&person::age, &person::name>(person{"purecpp_bb", 222},
                                                      "id=2");
    vec1 = postgres.query_s<person>("id=$1", 1);
    vec2 = postgres.query_s<person>("id=$1", 2);
    CHECK(vec1.size() == 1);
    CHECK(vec2.size() == 1);
    CHECK(vec1.front().age == 111);
    CHECK(vec2.front().age == 222);
    CHECK(vec1.front().name == "purecpp_aa");
    CHECK(vec2.front().name == "purecpp_bb");
    postgres.update_some<&person::name, &person::age>(
        person{"purecpp_aaa", 333, 1});
    postgres.update_some<&person::name, &person::age>(
        person{"purecpp_bbb", 444}, "id=2");
    vec1 = postgres.query_s<person>("id=$1", 1);
    vec2 = postgres.query_s<person>("id=$1", 2);
    CHECK(vec1.size() == 1);
    CHECK(vec2.size() == 1);
    CHECK(vec1.front().age == 333);
    CHECK(vec2.front().age == 444);
    CHECK(vec1.front().name == "purecpp_aaa");
    CHECK(vec2.front().name == "purecpp_bbb");
    postgres.update_some<&person::name, &person::age>(std::vector<person>{
        {"purecpp_aaaa", 555, 1}, {"purecpp_bbbb", 666, 2}});
    vec1 = postgres.query_s<person>("id=$1", 1);
    vec2 = postgres.query_s<person>("id=$1", 2);
    CHECK(vec1.size() == 1);
    CHECK(vec2.size() == 1);
    CHECK(vec1.front().age == 555);
    CHECK(vec2.front().age == 666);
    CHECK(vec1.front().name == "purecpp_aaaa");
    CHECK(vec2.front().name == "purecpp_bbbb");
  }
#endif
#ifdef ORMPP_ENABLE_SQLITE3
  dbng<sqlite> sqlite;
#ifdef SQLITE_HAS_CODEC
  if (sqlite.connect(db, password)) {
#else
  if (sqlite.connect(db)) {
#endif
    sqlite.execute("drop table if exists person");
    sqlite.create_datatable<person>(ormpp_auto_key{"id"});
    sqlite.insert<person>({"person_a", 1});
    sqlite.insert<person>({"person_b", 2});
    sqlite.update_some<&person::name>(person{"purecpp_a", 0, 1});
    sqlite.update_some<&person::name>(person{"purecpp_b"}, "id=2");
    auto vec1 = sqlite.query_s<person>("id=?", 1);
    auto vec2 = sqlite.query_s<person>("id=?", 2);
    CHECK(vec1.size() == 1);
    CHECK(vec2.size() == 1);
    CHECK(vec1.front().name == "purecpp_a");
    CHECK(vec2.front().name == "purecpp_b");
    sqlite.update_some<&person::age, &person::name>(
        person{"purecpp_aa", 111, 1});
    sqlite.update_some<&person::age, &person::name>(person{"purecpp_bb", 222},
                                                    "id=2");
    vec1 = sqlite.query_s<person>("id=?", 1);
    vec2 = sqlite.query_s<person>("id=?", 2);
    CHECK(vec1.size() == 1);
    CHECK(vec2.size() == 1);
    CHECK(vec1.front().age == 111);
    CHECK(vec2.front().age == 222);
    CHECK(vec1.front().name == "purecpp_aa");
    CHECK(vec2.front().name == "purecpp_bb");
    sqlite.update_some<&person::name, &person::age>(
        person{"purecpp_aaa", 333, 1});
    sqlite.update_some<&person::name, &person::age>(person{"purecpp_bbb", 444},
                                                    "id=2");
    vec1 = sqlite.query_s<person>("id=?", 1);
    vec2 = sqlite.query_s<person>("id=?", 2);
    CHECK(vec1.size() == 1);
    CHECK(vec2.size() == 1);
    CHECK(vec1.front().age == 333);
    CHECK(vec2.front().age == 444);
    CHECK(vec1.front().name == "purecpp_aaa");
    CHECK(vec2.front().name == "purecpp_bbb");
    sqlite.update_some<&person::name, &person::age>(std::vector<person>{
        {"purecpp_aaaa", 555, 1}, {"purecpp_bbbb", 666, 2}});
    vec1 = sqlite.query_s<person>("id=?", 1);
    vec2 = sqlite.query_s<person>("id=?", 2);
    CHECK(vec1.size() == 1);
    CHECK(vec2.size() == 1);
    CHECK(vec1.front().age == 555);
    CHECK(vec2.front().age == 666);
    CHECK(vec1.front().name == "purecpp_aaaa");
    CHECK(vec2.front().name == "purecpp_bbbb");
  }
#endif
}
#endif

struct unsigned_type_t {
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
REGISTER_AUTO_KEY(unsigned_type_t, id)

TEST_CASE("unsigned type") {
#ifdef ORMPP_ENABLE_MYSQL
  dbng<mysql> mysql;
  if (mysql.connect(ip, username, password, db)) {
    mysql.execute("drop table if exists unsigned_type_t");
    mysql.create_datatable<unsigned_type_t>();
    auto id = mysql.get_insert_id_after_insert(
        unsigned_type_t{1, 2, 3, 4, 5, 6, 7, 8, "purecpp"});
    auto vec = mysql.query_s<unsigned_type_t>();
    CHECK(vec.size() == 1);
    CHECK(vec.front().a == 1);
    CHECK(vec.front().b == 2);
    CHECK(vec.front().c == 3);
    CHECK(vec.front().d == 4);
    CHECK(vec.front().e == 5);
    CHECK(vec.front().f == 6);
    CHECK(vec.front().g == 7);
    CHECK(vec.front().h == 8);
    CHECK(vec.front().v == "purecpp");
  }
#endif
#ifdef ORMPP_ENABLE_PG
  dbng<postgresql> postgres;
  if (postgres.connect(ip, username, password, db)) {
    postgres.execute("drop table if exists unsigned_type_t");
    postgres.create_datatable<unsigned_type_t>();
    auto id = postgres.get_insert_id_after_insert(
        unsigned_type_t{1, 2, 3, 4, 5, 6, 7, 8, "purecpp"});
    auto vec = postgres.query_s<unsigned_type_t>();
    CHECK(vec.size() == 1);
    CHECK(vec.front().a == 1);
    CHECK(vec.front().b == 2);
    CHECK(vec.front().c == 3);
    CHECK(vec.front().d == 4);
    CHECK(vec.front().e == 5);
    CHECK(vec.front().f == 6);
    CHECK(vec.front().g == 7);
    CHECK(vec.front().h == 8);
    CHECK(vec.front().v == "purecpp");
  }
#endif
#ifdef ORMPP_ENABLE_SQLITE3
  dbng<sqlite> sqlite;
#ifdef SQLITE_HAS_CODEC
  if (sqlite.connect(db, password)) {
#else
  if (sqlite.connect(db)) {
#endif
    sqlite.execute("drop table if exists unsigned_type_t");
    sqlite.create_datatable<unsigned_type_t>();
    auto id = sqlite.get_insert_id_after_insert(
        unsigned_type_t{1, 2, 3, 4, 5, 6, 7, 8, "purecpp"});
    auto vec = sqlite.query_s<unsigned_type_t>();
    CHECK(id == 1);
    CHECK(vec.size() == 1);
    CHECK(vec.front().a == 1);
    CHECK(vec.front().b == 2);
    CHECK(vec.front().c == 3);
    CHECK(vec.front().d == 4);
    CHECK(vec.front().e == 5);
    CHECK(vec.front().f == 6);
    CHECK(vec.front().g == 7);
    CHECK(vec.front().h == 8);
    CHECK(vec.front().v == "purecpp");
  }
#endif
}

#if __cplusplus >= 202002L

struct region_model {
  int id;
  double d_score;
  float f_score;
  std::string name;
  static constexpr std::string_view get_alias_struct_name(region_model *) {
    return "region";
  }
  int get_id() const { return id; }

  std::string to_json() {
    iguana::string_stream json;
    iguana::to_json(*this, json);
    return std::move(json);
  }
};
// REGISTER_AUTO_KEY(region_model, id)
REGISTER_CONFLICT_KEY(region_model, id)

TEST_CASE("struct with function") {
  std::string region_type = "region_type";
  region_model region;
  region.id = 1;
  region.d_score = 1.1;
  region.f_score = 1.2f;
  region.name = "region_name";
  CHECK(region.name == "region_name");
  std::cout << region.to_json() << std::endl;

#ifdef ORMPP_ENABLE_MYSQL
  dbng<mysql> mysql;
  if (mysql.connect(ip, username, password, db)) {
    mysql.execute("drop table if exists region");
    mysql.create_datatable<region_model>();
    mysql.insert(region);

    auto vec = mysql.query_s<region_model>();
    CHECK(vec.size() == 1);
    region.name = "purecpp";
    mysql.update_some<&region_model::name>(region);
    vec.clear();
    CHECK(vec.empty());
    vec = mysql.query_s<region_model>();
    CHECK(vec.size() == 1);
    CHECK(vec.front().name == "purecpp");
    CHECK(mysql.delete_records_s<region_model>("name=?", "purecpp") == 1);
  }
#endif

#ifdef ORMPP_ENABLE_PG
  dbng<postgresql> postgres;
  if (postgres.connect(ip, username, password, db)) {
    postgres.execute("drop table if exists region");
    postgres.create_datatable<region_model>();
    postgres.insert(region);
    auto vec = postgres.query_s<region_model>();
    CHECK(vec.size() == 1);

    region.name = "purecpp";
    postgres.update_some<&region_model::name>(region);
    vec.clear();
    CHECK(vec.empty());
    vec = postgres.query_s<region_model>();
    CHECK(vec.size() == 1);
    CHECK(vec.front().name == "purecpp");
    CHECK(postgres.delete_records_s<region_model>("name=$1", "purecpp") == 1);
  }
#endif

#ifdef ORMPP_ENABLE_SQLITE3
  dbng<sqlite> sqlite;
#ifdef SQLITE_HAS_CODEC
  if (sqlite.connect(db, password)) {
#else
  if (sqlite.connect(db)) {
#endif
    auto result = sqlite.execute("drop table if exists region");
    CHECK(result);
    result = sqlite.create_datatable<region_model>();
    CHECK(result);
    CHECK(sqlite.insert(region));
    auto vec = sqlite.query_s<region_model>();
    CHECK(vec.size() == 1);

    region.name = "purecpp";
    sqlite.update_some<&region_model::name>(region);
    vec.clear();
    CHECK(vec.empty());
    vec = sqlite.query_s<region_model>();
    CHECK(vec.size() == 1);
    CHECK(vec.front().name == "purecpp");
    CHECK(sqlite.delete_records_s<region_model>("name=?", "purecpp") == 1);
  }
#endif
}
#endif