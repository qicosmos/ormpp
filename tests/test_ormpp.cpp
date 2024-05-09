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
REFLECTION(person, id, name, age)

struct student {
  int code;
  std::string name;
  char sex;
  int age;
  double dm;
  std::string classroom;
};
REGISTER_CONFLICT_KEY(student, code)
REFLECTION(student, code, name, sex, age, dm, classroom)

struct simple {
  int id;
  double code;
  int age;
};
REFLECTION(simple, id, code, age)

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
//        REQUIRE(!mysql.query<student>("limit 1000").empty());
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
REFLECTION(test_optional, id, name, age, empty_);

TEST_CASE("optional") {
#ifdef ORMPP_ENABLE_MYSQL
  dbng<mysql> mysql;
  if (mysql.connect(ip, username, password, db)) {
    mysql.create_datatable<test_optional>(ormpp_auto_key{"id"});
    mysql.delete_records<test_optional>();
    mysql.insert<test_optional>({0, "purecpp", 200});
    auto vec1 = mysql.query<test_optional>();
    REQUIRE(vec1.size() > 0);
    CHECK(vec1.front().age.value() == 200);
    CHECK(vec1.front().name.value() == "purecpp");
    CHECK(vec1.front().empty_.has_value() == false);
    auto vec2 = mysql.query<test_optional>("select * from test_optional;");
    REQUIRE(vec2.size() > 0);
    CHECK(vec2.front().age.value() == 200);
    CHECK(vec2.front().name.value() == "purecpp");
    CHECK(vec2.front().empty_.has_value() == false);
  }
#endif
#ifdef ORMPP_ENABLE_PG
  dbng<postgresql> postgres;
  if (postgres.connect(ip, username, password, db)) {
    postgres.create_datatable<test_optional>(ormpp_auto_key{"id"});
    postgres.delete_records<test_optional>();
    postgres.insert<test_optional>({0, "purecpp", 200});
    auto vec1 = postgres.query<test_optional>();
    REQUIRE(vec1.size() > 0);
    CHECK(vec1.front().age.value() == 200);
    CHECK(vec1.front().name.value() == "purecpp");
    CHECK(vec1.front().empty_.has_value() == false);
    auto vec2 = postgres.query<test_optional>("select * from test_optional;");
    REQUIRE(vec2.size() > 0);
    CHECK(vec2.front().age.value() == 200);
    CHECK(vec2.front().name.value() == "purecpp");
    CHECK(vec2.front().empty_.has_value() == false);
  }
#endif
#ifdef ORMPP_ENABLE_SQLITE3
  dbng<sqlite> sqlite;
  if (sqlite.connect(db)) {
    sqlite.create_datatable<test_optional>(ormpp_auto_key{"id"});
    sqlite.delete_records<test_optional>();
    sqlite.insert<test_optional>({0, "purecpp", 200});
    auto vec1 = sqlite.query<test_optional>();
    REQUIRE(vec1.size() > 0);
    CHECK(vec1.front().age.value() == 200);
    CHECK(vec1.front().name.value() == "purecpp");
    CHECK(vec1.front().empty_.has_value() == false);
    auto vec2 = sqlite.query<test_optional>("select * from test_optional;");
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
REFLECTION(test_order, name, id);

TEST_CASE("random reflection order") {
#ifdef ORMPP_ENABLE_MYSQL
  dbng<mysql> mysql;
  if (mysql.connect(ip, username, password, db,
                    /*timeout_seconds=*/5, 3306)) {
    REQUIRE(mysql.execute(
        "create table if not exists `test_order` (id int, name text);"));
    mysql.delete_records<test_order>();
    int id = 666;
    std::string name = "hello";
    mysql.insert(test_order{id, name});
    auto vec = mysql.query<test_order>();
    REQUIRE(vec.size() > 0);
    CHECK(vec.front().id == id);
    CHECK(vec.front().name == name);
  }
#endif
}

struct custom_name {
  int id;
  std::string name;
};
REFLECTION_WITH_NAME(custom_name, "test_order", id, name);

TEST_CASE("custom name") {
#ifdef ORMPP_ENABLE_MYSQL
  dbng<mysql> mysql;
  if (mysql.connect(ip, username, password, db)) {
    auto vec1 = mysql.query<custom_name>();
    CHECK(vec1.size() > 0);
    auto vec2 = mysql.query(FID(custom_name::name), "=", "hello");
    CHECK(vec2.size() > 0);
  }
#endif
}

struct dummy {
  int id;
  std::string name;
};
REFLECTION(dummy, id, name);

// TEST_CASE("mysql exist tb") {
//   dbng<mysql> mysql;
//   REQUIRE(mysql.connect(ip, username, password, db,
//                         /*timeout_seconds=*/5, 3306));
//   dummy d{0, "tom"};
//   dummy d1{0, "jerry"};
//   mysql.insert(d);
//   mysql.insert(d1);
//   auto v = mysql.query<dummy>("limit 1, 1");
//   std::cout << v.size() << "\n";
// }

// #ifdef ORMPP_ENABLE_MYSQL
// TEST_CASE("mysql pool") {
//	dbng<sqlite> sqlite;
//	sqlite.connect(db);
//	sqlite.create_datatable<test_tb>(ormpp_unique{{"name"}});
//	test_tb tb{ 1, "aa" };
//	sqlite.insert(tb);
//	auto vt = sqlite.query<test_tb>();
//	auto vt1 = sqlite.query<std::tuple<test_tb>>("select * from test_tb");
//    auto& pool = connection_pool<dbng<mysql>>::instance();
//    try {
//        pool.init(1, ip, username, password, db, 2);
//    }catch(const std::exception& e){
//        std::cout<<e.what()<<std::endl;
//        return;
//    }
//	auto con = pool.get();
//	auto v = con->query<std::tuple<test_tb>>("select * from test_tb");
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
  auto vec = conn1->query<student>();
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
  REQUIRE(sqlite.connect(db));
  REQUIRE(sqlite.disconnect());
  REQUIRE(sqlite.connect(db));
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
  REQUIRE(sqlite.connect(db));
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
  ormpp_key key{"code"};
  ormpp_not_null not_null{{"code", "age"}};
  ormpp_auto_key auto_key{"code"};

  student s = {1, "tom", 0, 19, 1.5, "room2"};
  student s1 = {2, "jack", 1, 20, 2.5, "room3"};
  student s2 = {3, "mke", 2, 21, 3.5, "room4"};
  std::vector<student> v{s1, s2};

#ifdef ORMPP_ENABLE_MYSQL
  dbng<mysql> mysql;
  if (mysql.connect(ip, username, password, db)) {
    auto vec = mysql.query(FID(person::id), "<", "5");
  }
#endif

#ifdef ORMPP_ENABLE_PG
  dbng<postgresql> postgres;
  if (postgres.connect(ip, username, password, db)) {
    auto vec = postgres.query(FID(person::id), "<", "5");
  }
#endif

#ifdef ORMPP_ENABLE_SQLITE3
  dbng<sqlite> sqlite;
  if (sqlite.connect(db)) {
    auto vec = sqlite.query(FID(person::id), "<", "5");
  }
#endif

  // auto key
  {
#ifdef ORMPP_ENABLE_MYSQL
    mysql.execute("drop table if exists student;");
    mysql.create_datatable<student>(auto_key, not_null);
    CHECK(mysql.insert(s) == 1);
    auto vec1 = mysql.query<student>();
    CHECK(vec1.size() == 1);
    CHECK(mysql.insert(v) == 2);
    auto vec2 = mysql.query<student>();
    CHECK(vec2.size() == 3);
    auto vec3 = mysql.query(FID(student::code), "<", "5");
    CHECK(vec3.size() == 3);
    auto vec4 = mysql.query<student>("limit 2");
    CHECK(vec4.size() == 2);
#endif

#ifdef ORMPP_ENABLE_PG
    postgres.execute("drop table if exists student;");
    postgres.create_datatable<student>(auto_key, not_null);
    CHECK(postgres.insert(s) == 1);
    auto vec1 = postgres.query<student>();
    CHECK(vec1.size() == 1);
    CHECK(postgres.insert(v) == 2);
    auto vec2 = postgres.query<student>();
    CHECK(vec2.size() == 3);
    auto vec3 = postgres.query(FID(student::code), "<", "5");
    CHECK(vec3.size() == 3);
    auto vec4 = postgres.query<student>("limit 2");
    CHECK(vec4.size() == 2);
#endif

#ifdef ORMPP_ENABLE_SQLITE3
    sqlite.execute("drop table if exists student;");
    sqlite.create_datatable<student>(auto_key, not_null);
    CHECK(sqlite.insert(s) == 1);
    auto vec1 = sqlite.query<student>();
    CHECK(vec1.size() == 1);
    CHECK(sqlite.insert(v) == 2);
    auto vec2 = sqlite.query<student>();
    CHECK(vec2.size() == 3);
    auto vec3 = sqlite.query(FID(student::code), "<", "5");
    CHECK(vec3.size() == 3);
    auto vec4 = sqlite.query<student>("limit 2");
    CHECK(vec4.size() == 2);
#endif
  }

  // key
  {
#ifdef ORMPP_ENABLE_MYSQL
    mysql.execute("drop table if exists student;");
    mysql.create_datatable<student>(key, not_null);
    CHECK(mysql.insert(s) == 1);
    auto vec = mysql.query<student>();
    CHECK(vec.size() == 1);
#endif

#ifdef ORMPP_ENABLE_PG
    postgres.execute("drop table if exists student;");
    postgres.create_datatable<student>(key, not_null);
    CHECK(postgres.insert(s) == 1);
    auto vec = postgres.query<student>();
    CHECK(vec.size() == 1);
#endif

#ifdef ORMPP_ENABLE_SQLITE3
    sqlite.execute("drop table if exists student;");
    sqlite.create_datatable<student>(key, not_null);
    CHECK(sqlite.insert(s) == 1);
    auto vec = sqlite.query<student>();
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
    auto vec = mysql.query<person>();
    CHECK(vec.size() == 1);
    vec.front().name = "update";
    vec.front().age = 200;
    mysql.update(vec.front());
    vec = mysql.query<person>();
    CHECK(vec.size() == 1);
    CHECK(vec.front().name == "update");
    CHECK(vec.front().age == 200);
    mysql.update<person>({"purecpp", 100, 1}, "id=1");
    vec = mysql.query<person>();
    CHECK(vec.size() == 1);
    CHECK(vec.front().name == "purecpp");
    CHECK(vec.front().age == 100);
    vec.front().name = "update";
    vec.front().age = 200;
    mysql.replace(vec.front());
    vec = mysql.query<person>();
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
    auto vec = postgres.query<person>();
    CHECK(vec.size() == 1);
    vec.front().name = "update";
    vec.front().age = 200;
    postgres.update(vec.front());
    vec = postgres.query<person>();
    CHECK(vec.size() == 1);
    CHECK(vec.front().name == "update");
    CHECK(vec.front().age == 200);
    postgres.update<person>({"purecpp", 100, 1}, "id=1");
    vec = postgres.query<person>();
    CHECK(vec.size() == 1);
    CHECK(vec.front().name == "purecpp");
    CHECK(vec.front().age == 100);
    vec.front().name = "update";
    vec.front().age = 200;
    postgres.replace(vec.front());
    vec = postgres.query<person>();
    CHECK(vec.size() == 1);
    CHECK(vec.front().name == "update");
    CHECK(vec.front().age == 200);
    postgres.replace<person>({"purecpp", 100, 1}, "id");
    vec = postgres.query<person>();
    CHECK(vec.size() == 1);
    CHECK(vec.front().name == "purecpp");
    CHECK(vec.front().age == 100);
  }
#endif
#ifdef ORMPP_ENABLE_SQLITE3
  dbng<sqlite> sqlite;
  if (sqlite.connect(db)) {
    sqlite.execute("drop table if exists person");
    sqlite.create_datatable<person>(ormpp_auto_key{"id"});
    sqlite.insert<person>({"purecpp", 100});
    auto vec = sqlite.query<person>();
    CHECK(vec.size() == 1);
    vec.front().name = "update";
    vec.front().age = 200;
    sqlite.update(vec.front());
    vec = sqlite.query<person>();
    CHECK(vec.size() == 1);
    CHECK(vec.front().name == "update");
    CHECK(vec.front().age == 200);
    sqlite.update<person>({"purecpp", 100, 1}, "id=1");
    vec = sqlite.query<person>();
    CHECK(vec.size() == 1);
    CHECK(vec.front().name == "purecpp");
    CHECK(vec.front().age == 100);
    vec.front().name = "update";
    vec.front().age = 200;
    sqlite.replace(vec.front());
    vec = sqlite.query<person>();
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
    mysql.create_datatable<student>(key, not_null);
    mysql.delete_records<student>();
    CHECK(mysql.insert(v) == 3);
    v[0].name = "test1";
    v[1].name = "test2";
    CHECK(mysql.update(v[0]) == 1);
    auto vec1 = mysql.query<student>();
    CHECK(mysql.update(v[1]) == 1);
    auto vec2 = mysql.query<student>();
  }
#endif

#ifdef ORMPP_ENABLE_PG
  dbng<postgresql> postgres;
  if (postgres.connect(ip, username, password, db)) {
    postgres.create_datatable<student>(key, not_null);
    postgres.delete_records<student>();
    CHECK(postgres.insert(v) == 3);
    v[0].name = "test1";
    v[1].name = "test2";
    CHECK(postgres.update(v[0]) == 1);
    auto vec1 = postgres.query<student>();
    CHECK(postgres.update(v[1]) == 1);
    auto vec2 = postgres.query<student>();
  }
#endif

#ifdef ORMPP_ENABLE_SQLITE3
  dbng<sqlite> sqlite;
  if (sqlite.connect(db)) {
    sqlite.create_datatable<student>(key);
    sqlite.delete_records<student>();
    CHECK(sqlite.insert(v) == 3);
    v[0].name = "test1";
    v[1].name = "test2";
    CHECK(sqlite.update(v[0]) == 1);
    auto vec1 = sqlite.query<student>();
    CHECK(sqlite.update(v[1]) == 1);
    auto vec2 = sqlite.query<student>();
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
    mysql.delete_records<student>();
    CHECK(mysql.insert(v) == 3);
    v[0].name = "test1";
    v[1].name = "test2";
    v[2].name = "test3";
    CHECK(mysql.update(v) == 3);
    auto vec = mysql.query<student>();
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
    auto vec = postgres.query<student>();
    CHECK(vec.size() == 3);
  }
#endif

#ifdef ORMPP_ENABLE_SQLITE3
  dbng<sqlite> sqlite;
  if (sqlite.connect(db)) {
    sqlite.execute("drop table if exists student");
    sqlite.create_datatable<student>(auto_key, not_null);
    CHECK(sqlite.insert(v) == 3);
    v[0].name = "test1";
    v[1].name = "test2";
    v[2].name = "test3";
    CHECK(sqlite.update(v) == 3);
    auto vec = sqlite.query<student>();
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
    mysql.create_datatable<student>(key, not_null);
    mysql.delete_records<student>();
    CHECK(mysql.insert(v) == 3);
    mysql.delete_records<student>("code=1");
    auto vec1 = mysql.query<student>();
    CHECK(vec1.size() == 2);
    mysql.delete_records<student>("");
    auto vec2 = mysql.query<student>();
    CHECK(vec2.size() == 0);
  }
#endif

#ifdef ORMPP_ENABLE_PG
  dbng<postgresql> postgres;
  if (postgres.connect(ip, username, password, db)) {
    postgres.create_datatable<student>(key, not_null);
    postgres.delete_records<student>();
    CHECK(postgres.insert(v) == 3);
    postgres.delete_records<student>("code=1");
    auto vec1 = postgres.query<student>();
    CHECK(vec1.size() == 2);
    postgres.delete_records<student>("");
    auto vec2 = postgres.query<student>();
    CHECK(vec2.size() == 0);
  }
#endif

#ifdef ORMPP_ENABLE_SQLITE3
  dbng<sqlite> sqlite;
  if (sqlite.connect(db)) {
    sqlite.create_datatable<student>(key);
    sqlite.delete_records<student>();
    CHECK(sqlite.insert(v) == 3);
    REQUIRE(sqlite.delete_records<student>("code=1"));
    auto vec1 = sqlite.query<student>();
    CHECK(vec1.size() == 2);
    REQUIRE(sqlite.delete_records<student>(""));
    auto vec2 = sqlite.query<student>();
    CHECK(vec2.size() == 0);
  }
#endif
}

TEST_CASE("query") {
  ormpp_key key{"id"};
  simple s1 = {1, 2.5, 3};
  simple s2 = {2, 3.5, 4};
  simple s3 = {3, 4.5, 5};
  std::vector<simple> v{s1, s2, s3};

#ifdef ORMPP_ENABLE_MYSQL
  dbng<mysql> mysql;
  if (mysql.connect(ip, username, password, db)) {
    mysql.create_datatable<simple>(key);
    mysql.delete_records<simple>();
    CHECK(mysql.insert(v) == 3);
    auto vec1 = mysql.query<simple>();
    CHECK(vec1.size() == 3);
    auto vec2 = mysql.query<simple>("id=1");
    CHECK(vec2.size() == 1);
  }
#endif

#ifdef ORMPP_ENABLE_PG
  dbng<postgresql> postgres;
  if (postgres.connect(ip, username, password, db)) {
    postgres.create_datatable<simple>(key);
    postgres.delete_records<simple>();
    CHECK(postgres.insert(v) == 3);
    auto vec1 = postgres.query<simple>();
    CHECK(vec1.size() == 3);
    auto vec2 = postgres.query<simple>("id=2");
    CHECK(vec2.size() == 1);
  }
#endif

#ifdef ORMPP_ENABLE_SQLITE3
  dbng<sqlite> sqlite;
  if (sqlite.connect(db)) {
    sqlite.create_datatable<simple>(key);
    sqlite.delete_records<simple>();
    CHECK(sqlite.insert(v) == 3);
    auto vec1 = sqlite.query<simple>();
    CHECK(vec1.size() == 3);
    auto vec2 = sqlite.query<simple>("id=3");
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
    mysql.create_datatable<student>(key, not_null);
    mysql.delete_records<student>();
    CHECK(mysql.insert(v) == 3);
    auto vec1 = mysql.query<std::tuple<int>>("select count(1) from student");
    CHECK(vec1.size() == 1);
    CHECK(std::get<0>(vec1[0]) == 3);
    auto vec2 = mysql.query<std::tuple<int, std::string, double>>(
        "select code, name, dm from student");
    CHECK(vec2.size() == 3);
  }
#endif

#ifdef ORMPP_ENABLE_PG
  dbng<postgresql> postgres;
  if (postgres.connect(ip, username, password, db)) {
    postgres.create_datatable<student>(key, not_null);
    postgres.delete_records<student>();
    CHECK(postgres.insert(v) == 3);
    auto vec1 = postgres.query<std::tuple<int>>("select count(1) from student");
    CHECK(vec1.size() == 1);
    CHECK(std::get<0>(vec1[0]) == 3);
    auto vec2 = postgres.query<std::tuple<int, std::string, double>>(
        "select code, name, dm from student");
    CHECK(vec2.size() == 3);
  }
#endif

#ifdef ORMPP_ENABLE_SQLITE3
  dbng<sqlite> sqlite;
  if (sqlite.connect(db)) {
    sqlite.create_datatable<student>(key);
    sqlite.delete_records<student>();
    CHECK(sqlite.insert(v) == 3);
    auto vec1 = sqlite.query<std::tuple<int>>("select count(1) from student");
    CHECK(vec1.size() == 1);
    CHECK(std::get<0>(vec1[0]) == 3);
    auto vec2 = sqlite.query<std::tuple<int, std::string, double>>(
        "select code, name, dm from student");
    CHECK(vec2.size() == 3);
  }
#endif
}

TEST_CASE("query multi table") {
  ormpp_key key{"code"};
  ormpp_not_null not_null{{"code", "age"}};
  ormpp_auto_key auto_key{"code"};

  student s = {1, "tom", 0, 19, 1.5, "room2"};
  student s1 = {2, "jack", 1, 20, 2.5, "room3"};
  student s2 = {3, "mike", 2, 21, 3.5, "room4"};
  std::vector<student> v{s, s1, s2};

  ormpp_key key1{"id"};
  person p = {"test1", 2, 1};
  person p1 = {"test2", 3, 2};
  person p2 = {"test3", 4, 3};
  std::vector<person> v1{p, p1, p2};

#ifdef ORMPP_ENABLE_MYSQL
  dbng<mysql> mysql;
  if (mysql.connect(ip, username, password, db)) {
    mysql.create_datatable<student>(key, not_null);
    mysql.create_datatable<person>(key1, not_null);
    mysql.delete_records<student>();
    mysql.delete_records<person>();
    CHECK(mysql.insert(v) == 3);
    CHECK(mysql.insert(v1) == 3);
    auto vec1 = mysql.query<std::tuple<person, std::string, int>>(
        "select person.*, student.name, student.age from person, student"s);
    CHECK(vec1.size() == 9);
    auto vec2 = mysql.query<std::tuple<person, student>>(
        "select * from person, student"s);
    CHECK(vec2.size() == 9);
  }
#endif

#ifdef ORMPP_ENABLE_PG
  dbng<postgresql> postgres;
  if (postgres.connect(ip, username, password, db)) {
    postgres.create_datatable<student>(key, not_null);
    postgres.create_datatable<person>(key1, not_null);
    postgres.delete_records<student>();
    postgres.delete_records<person>();
    CHECK(postgres.insert(v) == 3);
    CHECK(postgres.insert(v1) == 3);
    auto vec1 = postgres.query<std::tuple<int, std::string, double>>(
        "select person.*, student.name, student.age from person, student"s);
    CHECK(vec1.size() == 9);
    auto vec2 = postgres.query<std::tuple<person, student>>(
        "select * from person, student"s);
    CHECK(vec2.size() == 9);
  }
#endif

#ifdef ORMPP_ENABLE_SQLITE3
  dbng<sqlite> sqlite;
  if (sqlite.connect(db)) {
    sqlite.create_datatable<student>(key);
    sqlite.create_datatable<person>(key1);
    sqlite.delete_records<student>();
    sqlite.delete_records<person>();
    CHECK(sqlite.insert(v) == 3);
    CHECK(sqlite.insert(v1) == 3);
    auto vec1 = sqlite.query<std::tuple<int, std::string, double>>(
        "select person.*, student.name, student.age from person, student"s);
    CHECK(vec1.size() == 9);
    auto vec2 = sqlite.query<std::tuple<person, student>>(
        "select * from person, student"s);
    CHECK(vec2.size() == 9);
  }
#endif
}

TEST_CASE("transaction") {
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
    mysql.create_datatable<student>(key, not_null);
    mysql.delete_records<student>();
    mysql.begin();
    for (int i = 0; i < 10; ++i) {
      student st = {i + 1, "tom", 0, 19, 1.5, "room2"};
      if (!mysql.insert(st)) {
        mysql.rollback();
        return;
      }
    }
    mysql.commit();
    auto vec = mysql.query<student>();
    CHECK(vec.size() == 10);
  }
#endif

#ifdef ORMPP_ENABLE_PG
  dbng<postgresql> postgres;
  if (postgres.connect(ip, username, password, db)) {
    postgres.create_datatable<student>(key, not_null);
    postgres.delete_records<student>();
    postgres.begin();
    for (int i = 0; i < 10; ++i) {
      student s = {i + 1, "tom", 0, 19, 1.5, "room2"};
      if (!postgres.insert(s)) {
        postgres.rollback();
        return;
      }
    }
    postgres.commit();
    auto vec = postgres.query<student>();
    CHECK(vec.size() == 10);
  }
#endif

#ifdef ORMPP_ENABLE_SQLITE3
  dbng<sqlite> sqlite;
  if (sqlite.connect(db)) {
    sqlite.create_datatable<student>(key);
    sqlite.delete_records<student>();
    sqlite.begin();
    for (int i = 0; i < 10; ++i) {
      student st = {i + 1, "tom", 0, 19, 1.5, "room2"};
      if (!sqlite.insert(st)) {
        sqlite.rollback();
        return;
      }
    }
    sqlite.commit();
    auto vec = sqlite.query<student>();
    CHECK(vec.size() == 10);
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

#ifdef ORMPP_ENABLE_MYSQL
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
REFLECTION(image, id, bin);

TEST_CASE("mysql blob") {
  dbng<mysql> mysql;
  if (mysql.connect(ip, username, password, db)) {
    mysql.execute("DROP TABLE IF EXISTS image");
    mysql.create_datatable<image>();
    auto data = "this is a test binary stream\0, and ?...";
    auto size = 40;
    image img;
    img.id = 1;
    img.bin.assign(data, data + size);
    CHECK(mysql.insert(img) == 1);
    auto vec = mysql.query<image>("id=1");
    CHECK(vec.size() == 1);
    CHECK(vec[0].bin.size() == size);
  }
}

struct image_ex {
  int id;
  ormpp::blob bin;
  std::string time;
};
REFLECTION(image_ex, id, bin, time);

TEST_CASE("mysql blob tuple") {
  dbng<mysql> mysql;
  if (mysql.connect(ip, username, password, db)) {
    mysql.execute("DROP TABLE IF EXISTS image_ex");
    mysql.create_datatable<image_ex>();
    auto data = "this is a test binary stream\0, and ?...";
    auto size = 40;
    image_ex img_ex;
    img_ex.id = 1;
    img_ex.bin.assign(data, data + size);
    img_ex.time = "2023-03-29 13:55:00";
    CHECK(mysql.insert(img_ex) == 1);
    auto vec1 = mysql.query<image_ex>("id=1");
    CHECK(vec1.size() == 1);
    CHECK(vec1[0].bin.size() == size);
    using image_t = std::tuple<image, std::string>;
    auto vec2 =
        mysql.query<image_t>("select id,bin,time from image_ex where id=1;");
    CHECK(vec2.size() == 1);
    auto &img = std::get<0>(vec2[0]);
    auto &time = std::get<1>(vec2[0]);
    CHECK(img.id == 1);
    CHECK(img.bin.size() == size);
    CHECK(time == img_ex.time);
  }
}
#endif

TEST_CASE("create table with unique") {
#ifdef ORMPP_ENABLE_MYSQL
  dbng<mysql> mysql;
  if (mysql.connect(ip, username, password, db)) {
    mysql.execute("drop table if exists person");
    mysql.create_datatable<person>(ormpp_auto_key{"id"},
                                   ormpp_unique{{"name", "age"}});
    mysql.insert<person>({"purecpp"});
    auto vec1 = mysql.query<person>("order by id");
    auto vec2 = mysql.query<person>("limit 1");
    CHECK(vec1.size() == 1);
    CHECK(vec2.size() == 1);
    mysql.insert<person>({"purecpp"});
    auto vec3 = mysql.query<person>();
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
    auto vec1 = postgres.query<person>("order by id");
    auto vec2 = postgres.query<person>("limit 1");
    CHECK(vec1.size() == 1);
    CHECK(vec2.size() == 1);
    postgres.insert<person>({"purecpp"});
    auto vec3 = postgres.query<person>();
    CHECK(vec3.size() == 1);
  }
#endif
#ifdef ORMPP_ENABLE_SQLITE3
  dbng<sqlite> sqlite;
  if (sqlite.connect(db)) {
    sqlite.execute("drop table if exists person");
    sqlite.create_datatable<person>(ormpp_auto_key{"id"},
                                    ormpp_unique{{"name", "age"}});
    sqlite.insert<person>({"purecpp"});
    auto vec1 = sqlite.query<person>("order by id");
    auto vec2 = sqlite.query<person>("limit 1");
    CHECK(vec1.size() == 1);
    CHECK(vec2.size() == 1);
    sqlite.insert<person>({"purecpp"});
    auto vec3 = sqlite.query<person>();
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
  if (sqlite.connect(db)) {
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
    mysql.delete_records_s<person>("name=?", "purecpp' or '1=1");
    auto vec8 = mysql.query_s<person>();
    mysql.delete_records_s<person>("name=?", "purecpp");
    auto vec9 = mysql.query_s<person>();
    mysql.delete_records_s<person>();
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
    postgres.delete_records_s<person>("name=$1", "purecpp' or '1=1");
    auto vec8 = postgres.query_s<person>();
    postgres.delete_records_s<person>("name=$1", "purecpp");
    auto vec9 = postgres.query_s<person>();
    postgres.delete_records_s<person>();
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
  if (sqlite.connect(db)) {
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
    auto vec7 = sqlite.query_s<person>("name=?", "purecpp' or '1=1");
    sqlite.delete_records_s<person>("name=?", "purecpp' or '1=1");
    auto vec8 = sqlite.query_s<person>();
    sqlite.delete_records_s<person>("name=?", "purecpp");
    auto vec9 = sqlite.query_s<person>();
    sqlite.delete_records_s<person>();
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
  }
#endif
}

struct tuple_optional_t {
  std::optional<std::string> name;
  std::optional<int> age;
  int id;
};
REGISTER_AUTO_KEY(tuple_optional_t, id)
REFLECTION(tuple_optional_t, id, name, age)

TEST_CASE("query tuple_optional_t") {
#ifdef ORMPP_ENABLE_MYSQL
  dbng<mysql> mysql;
  if (mysql.connect(ip, username, password, db)) {
    mysql.execute("drop table if exists tuple_optional_t");
    mysql.create_datatable<tuple_optional_t>(ormpp_auto_key{"id"});
    mysql.insert<tuple_optional_t>({"purecpp", 6});
    mysql.insert<tuple_optional_t>({std::nullopt});
    auto vec =
        mysql.query<std::tuple<tuple_optional_t, std::optional<std::string>,
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
    postgres.insert<tuple_optional_t>({"purecpp", 6});
    postgres.insert<tuple_optional_t>({std::nullopt});
    auto vec =
        postgres.query<std::tuple<tuple_optional_t, std::optional<std::string>,
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
#ifdef ORMPP_ENABLE_SQLITE3
  dbng<sqlite> sqlite;
  if (sqlite.connect(db)) {
    sqlite.execute("drop table if exists tuple_optional_t");
    sqlite.create_datatable<tuple_optional_t>(ormpp_auto_key{"id"});
    sqlite.insert<tuple_optional_t>({"purecpp", 6});
    sqlite.insert<tuple_optional_t>({std::nullopt});
    auto vec =
        sqlite.query<std::tuple<tuple_optional_t, std::optional<std::string>,
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
REFLECTION(test_enum_t, id, color, fruit)

TEST_CASE("test enum") {
#ifdef ORMPP_ENABLE_MYSQL
  dbng<mysql> mysql;
  if (mysql.connect(ip, username, password, db)) {
    mysql.execute("drop table if exists test_enum_t");
    mysql.create_datatable<test_enum_t>(ormpp_auto_key{"id"});
    mysql.insert<test_enum_t>({Color::BLUE});
    auto vec = mysql.query<test_enum_t>();
    CHECK(vec.size() == 1);
    CHECK(vec.front().color == Color::BLUE);
    CHECK(vec.front().fruit == APPLE);
    vec.front().color = Color::RED;
    vec.front().fruit = BANANA;
    mysql.update(vec.front());
    vec = mysql.query<test_enum_t>();
    CHECK(vec.size() == 1);
    CHECK(vec.front().color == Color::RED);
    CHECK(vec.front().fruit == BANANA);
    mysql.update<test_enum_t>({Color::BLUE, APPLE, 1}, "id=1");
    vec = mysql.query<test_enum_t>();
    CHECK(vec.size() == 1);
    CHECK(vec.front().color == Color::BLUE);
    CHECK(vec.front().fruit == APPLE);
    vec.front().color = Color::RED;
    vec.front().fruit = BANANA;
    mysql.replace(vec.front());
    vec = mysql.query<test_enum_t>();
    CHECK(vec.size() == 1);
    CHECK(vec.front().color == Color::RED);
    CHECK(vec.front().fruit == BANANA);
    mysql.delete_records<test_enum_t>();
    vec = mysql.query<test_enum_t>();
    CHECK(vec.size() == 0);
  }
#endif
#ifdef ORMPP_ENABLE_PG
  dbng<postgresql> postgres;
  if (postgres.connect(ip, username, password, db)) {
    postgres.execute("drop table if exists test_enum_t");
    postgres.create_datatable<test_enum_t>(ormpp_auto_key{"id"});
    postgres.insert<test_enum_t>({Color::BLUE});
    auto vec = postgres.query<test_enum_t>();
    CHECK(vec.size() == 1);
    CHECK(vec.front().color == Color::BLUE);
    CHECK(vec.front().fruit == APPLE);
    vec.front().color = Color::RED;
    vec.front().fruit = BANANA;
    postgres.update(vec.front());
    vec = postgres.query<test_enum_t>();
    CHECK(vec.size() == 1);
    CHECK(vec.front().color == Color::RED);
    CHECK(vec.front().fruit == BANANA);
    postgres.update<test_enum_t>({Color::BLUE, APPLE, 1}, "id=1");
    vec = postgres.query<test_enum_t>();
    CHECK(vec.size() == 1);
    CHECK(vec.front().color == Color::BLUE);
    CHECK(vec.front().fruit == APPLE);
    vec.front().color = Color::RED;
    vec.front().fruit = BANANA;
    postgres.replace(vec.front());
    vec = postgres.query<test_enum_t>();
    CHECK(vec.size() == 1);
    CHECK(vec.front().color == Color::RED);
    CHECK(vec.front().fruit == BANANA);
    postgres.delete_records<test_enum_t>();
    vec = postgres.query<test_enum_t>();
    CHECK(vec.size() == 0);
  }
#endif
#ifdef ORMPP_ENABLE_SQLITE3
  dbng<sqlite> sqlite;
  if (sqlite.connect(db)) {
    sqlite.execute("drop table if exists test_enum_t");
    sqlite.create_datatable<test_enum_t>(ormpp_auto_key{"id"});
    sqlite.insert<test_enum_t>({Color::BLUE});
    auto vec = sqlite.query<test_enum_t>();
    CHECK(vec.size() == 1);
    CHECK(vec.front().color == Color::BLUE);
    CHECK(vec.front().fruit == APPLE);
    vec.front().color = Color::RED;
    vec.front().fruit = BANANA;
    sqlite.update(vec.front());
    vec = sqlite.query<test_enum_t>();
    CHECK(vec.size() == 1);
    CHECK(vec.front().color == Color::RED);
    CHECK(vec.front().fruit == BANANA);
    sqlite.update<test_enum_t>({Color::BLUE, APPLE, 1}, "id=1");
    vec = sqlite.query<test_enum_t>();
    CHECK(vec.size() == 1);
    CHECK(vec.front().color == Color::BLUE);
    CHECK(vec.front().fruit == APPLE);
    vec.front().color = Color::RED;
    vec.front().fruit = BANANA;
    sqlite.replace(vec.front());
    vec = sqlite.query<test_enum_t>();
    CHECK(vec.size() == 1);
    CHECK(vec.front().color == Color::RED);
    CHECK(vec.front().fruit == BANANA);
    sqlite.delete_records<test_enum_t>();
    vec = sqlite.query<test_enum_t>();
    CHECK(vec.size() == 0);
  }
#endif
}

struct test_enum_with_name_t {
  Color color;
  Fruit fruit;
  int id;
};
REGISTER_AUTO_KEY(test_enum_with_name_t, id)
REFLECTION_WITH_NAME(test_enum_with_name_t, "test_enum", id, color, fruit)

TEST_CASE("test enum with custom name") {
#ifdef ORMPP_ENABLE_MYSQL
  dbng<mysql> mysql;
  if (mysql.connect(ip, username, password, db)) {
    mysql.execute("drop table if exists test_enum");
    mysql.create_datatable<test_enum_with_name_t>(ormpp_auto_key{"id"});
    mysql.insert<test_enum_with_name_t>({Color::BLUE});
    auto vec = mysql.query<test_enum_with_name_t>();
    CHECK(vec.size() == 1);
    CHECK(vec.front().color == Color::BLUE);
    CHECK(vec.front().fruit == APPLE);
    vec.front().color = Color::RED;
    vec.front().fruit = BANANA;
    mysql.update(vec.front());
    vec = mysql.query<test_enum_with_name_t>();
    CHECK(vec.size() == 1);
    CHECK(vec.front().color == Color::RED);
    CHECK(vec.front().fruit == BANANA);
    mysql.update<test_enum_with_name_t>({Color::BLUE, APPLE, 1}, "id=1");
    vec = mysql.query<test_enum_with_name_t>();
    CHECK(vec.size() == 1);
    CHECK(vec.front().color == Color::BLUE);
    CHECK(vec.front().fruit == APPLE);
    vec.front().color = Color::RED;
    vec.front().fruit = BANANA;
    mysql.replace(vec.front());
    vec = mysql.query<test_enum_with_name_t>();
    CHECK(vec.size() == 1);
    CHECK(vec.front().color == Color::RED);
    CHECK(vec.front().fruit == BANANA);
    mysql.delete_records<test_enum_with_name_t>();
    vec = mysql.query<test_enum_with_name_t>();
    CHECK(vec.size() == 0);
  }
#endif
#ifdef ORMPP_ENABLE_PG
  dbng<postgresql> postgres;
  if (postgres.connect(ip, username, password, db)) {
    postgres.execute("drop table if exists test_enum");
    postgres.create_datatable<test_enum_with_name_t>(ormpp_auto_key{"id"});
    postgres.insert<test_enum_with_name_t>({Color::BLUE});
    auto vec = postgres.query<test_enum_with_name_t>();
    CHECK(vec.size() == 1);
    CHECK(vec.front().color == Color::BLUE);
    CHECK(vec.front().fruit == APPLE);
    vec.front().color = Color::RED;
    vec.front().fruit = BANANA;
    postgres.update(vec.front());
    vec = postgres.query<test_enum_with_name_t>();
    CHECK(vec.size() == 1);
    CHECK(vec.front().color == Color::RED);
    CHECK(vec.front().fruit == BANANA);
    postgres.update<test_enum_with_name_t>({Color::BLUE, APPLE, 1}, "id=1");
    vec = postgres.query<test_enum_with_name_t>();
    CHECK(vec.size() == 1);
    CHECK(vec.front().color == Color::BLUE);
    CHECK(vec.front().fruit == APPLE);
    vec.front().color = Color::RED;
    vec.front().fruit = BANANA;
    postgres.replace(vec.front());
    vec = postgres.query<test_enum_with_name_t>();
    CHECK(vec.size() == 1);
    CHECK(vec.front().color == Color::RED);
    CHECK(vec.front().fruit == BANANA);
    postgres.delete_records<test_enum_with_name_t>();
    vec = postgres.query<test_enum_with_name_t>();
    CHECK(vec.size() == 0);
  }
#endif
#ifdef ORMPP_ENABLE_SQLITE3
  dbng<sqlite> sqlite;
  if (sqlite.connect(db)) {
    sqlite.execute("drop table if exists test_enum");
    sqlite.create_datatable<test_enum_with_name_t>(ormpp_auto_key{"id"});
    sqlite.insert<test_enum_with_name_t>({Color::BLUE});
    auto vec = sqlite.query<test_enum_with_name_t>();
    CHECK(vec.size() == 1);
    CHECK(vec.front().color == Color::BLUE);
    CHECK(vec.front().fruit == APPLE);
    vec.front().color = Color::RED;
    vec.front().fruit = BANANA;
    sqlite.update(vec.front());
    vec = sqlite.query<test_enum_with_name_t>();
    CHECK(vec.size() == 1);
    CHECK(vec.front().color == Color::RED);
    CHECK(vec.front().fruit == BANANA);
    sqlite.update<test_enum_with_name_t>({Color::BLUE, APPLE, 1}, "id=1");
    vec = sqlite.query<test_enum_with_name_t>();
    CHECK(vec.size() == 1);
    CHECK(vec.front().color == Color::BLUE);
    CHECK(vec.front().fruit == APPLE);
    vec.front().color = Color::RED;
    vec.front().fruit = BANANA;
    sqlite.replace(vec.front());
    vec = sqlite.query<test_enum_with_name_t>();
    CHECK(vec.size() == 1);
    CHECK(vec.front().color == Color::RED);
    CHECK(vec.front().fruit == BANANA);
    sqlite.delete_records<test_enum_with_name_t>();
    vec = sqlite.query<test_enum_with_name_t>();
    CHECK(vec.size() == 0);
  }
#endif
}

struct test_bool_t {
  bool ok;
  int id;
};
REGISTER_AUTO_KEY(test_bool_t, id)
REFLECTION(test_bool_t, id, ok)

TEST_CASE("test bool") {
#ifdef ORMPP_ENABLE_MYSQL
  dbng<mysql> mysql;
  if (mysql.connect(ip, username, password, db)) {
    mysql.execute("drop table if exists test_bool_t");
    mysql.create_datatable<test_bool_t>(ormpp_auto_key{"id"});
    mysql.insert(test_bool_t{true});
    auto vec = mysql.query<test_bool_t>();
    CHECK(vec.size() == 1);
    CHECK(vec.front().ok == true);
    mysql.delete_records<test_bool_t>();
    mysql.insert(test_bool_t{false});
    vec = mysql.query<test_bool_t>();
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
    auto vec = postgres.query<test_bool_t>();
    CHECK(vec.size() == 1);
    CHECK(vec.front().ok == true);
    postgres.delete_records<test_bool_t>();
    postgres.insert(test_bool_t{false});
    vec = postgres.query<test_bool_t>();
    CHECK(vec.size() == 1);
    CHECK(vec.front().ok == false);
  }
#endif
#ifdef ORMPP_ENABLE_SQLITE3
  dbng<sqlite> sqlite;
  if (sqlite.connect(db)) {
    sqlite.execute("drop table if exists test_bool_t");
    sqlite.create_datatable<test_bool_t>(ormpp_auto_key{"id"});
    sqlite.insert(test_bool_t{true});
    auto vec = sqlite.query<test_bool_t>();
    CHECK(vec.size() == 1);
    CHECK(vec.front().ok == true);
    sqlite.delete_records<test_bool_t>();
    sqlite.insert(test_bool_t{false});
    vec = sqlite.query<test_bool_t>();
    CHECK(vec.size() == 1);
    CHECK(vec.front().ok == false);
  }
#endif
}

struct alias {
  std::string name;
  int id;
};
REGISTER_AUTO_KEY(alias, id)
REFLECTION_ALIAS(alias, "t_alias", FLDALIAS(&alias::id, "alias_id"),
                 FLDALIAS(&alias::name, "alias_name"));

TEST_CASE("alias") {
#ifdef ORMPP_ENABLE_MYSQL
  dbng<mysql> mysql;
  if (mysql.connect(ip, username, password, db)) {
    mysql.create_datatable<alias>(ormpp_auto_key{"alias_id"});
    mysql.delete_records<alias>();
    mysql.insert<alias>({"purecpp"});
    auto vec = mysql.query<alias>();
    CHECK(vec.front().name == "purecpp");
  }
#endif

#ifdef ORMPP_ENABLE_SQLITE3
  dbng<sqlite> sqlite;
  if (sqlite.connect(db)) {
    sqlite.create_datatable<alias>(ormpp_auto_key{"alias_id"});
    sqlite.delete_records<alias>();
    sqlite.insert<alias>({"purecpp"});
    auto vec = sqlite.query<alias>();
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
    auto vec1 = postgres.query<person>();
    CHECK(vec1.size() == 1);
    vec1.front().name = "other";
    postgres.update<person>(vec1.front());
    auto vec2 = postgres.query<person>();
    CHECK(vec2.size() == 1);
    CHECK(vec2.front().name == "other");
    CHECK(vec1.front().id == vec2.front().id);
  }
}
#endif

TEST_CASE("update section filed") {
#ifdef ORMPP_ENABLE_MYSQL
  // dbng<mysql> mysql;
  // if (mysql.connect(ip, username, password, db)) {
  //   mysql.execute("drop table if exists person");
  //   mysql.create_datatable<person>(ormpp_auto_key{"id"});
  //   mysql.insert<person>({"purecpp1", 1});
  //   mysql.insert<person>({"purecpp2", 2});
  //   mysql.update<&person::name>(person{"111", 0, 1});
  //   mysql.update<&person::name>(person{"222"}, "id=2");
  //   auto vec1 = mysql.query_s<person>("id=?", 1);
  //   auto vec2 = mysql.query_s<person>("id=?", 2);
  //   CHECK(vec1.size() == 1);
  //   CHECK(vec2.size() == 1);
  //   CHECK(vec1.front().name == "111");
  //   CHECK(vec2.front().name == "222");
  //   mysql.update<&person::name, &person::age>(person{"666", 666, 1});
  //   vec1 = mysql.query_s<person>("id=?", 1);
  //   CHECK(vec1.size() == 1);
  //   CHECK(vec1.front().age == 666);
  //   CHECK(vec1.front().name == "666");
  // }
#endif
#ifdef ORMPP_ENABLE_PG
  // dbng<postgresql> postgres;
  // if (postgres.connect(ip, username, password, db)) {
  //   postgres.execute("drop table if exists person");
  //   postgres.create_datatable<person>(ormpp_auto_key{"id"});
  //   postgres.insert<person>({"purecpp1", 1});
  //   postgres.insert<person>({"purecpp2", 2});
  //   postgres.update<&person::name>(person{"111", 0, 1});
  //   postgres.update<&person::name>(person{"222"}, "id=2");
  //   auto vec1 = postgres.query_s<person>("id=?", 1);
  //   auto vec2 = postgres.query_s<person>("id=?", 2);
  //   CHECK(vec1.size() == 1);
  //   CHECK(vec2.size() == 1);
  //   CHECK(vec1.front().name == "111");
  //   CHECK(vec2.front().name == "222");
  //   postgres.update<&person::name, &person::age>(person{"666", 666, 1});
  //   vec1 = postgres.query_s<person>("id=?", 1);
  //   CHECK(vec1.size() == 1);
  //   CHECK(vec1.front().age == 666);
  //   CHECK(vec1.front().name == "666");
  // }
#endif
#ifdef ORMPP_ENABLE_SQLITE3
  dbng<sqlite> sqlite;
  if (sqlite.connect(db)) {
    sqlite.execute("drop table if exists person");
    sqlite.create_datatable<person>(ormpp_auto_key{"id"});
    sqlite.insert<person>({"person_a", 1});
    sqlite.insert<person>({"person_b", 2});
    auto vec3 = sqlite.query_s<person>();
    sqlite.update_s<&person::name>(person{"purecpp_a", 0, 1});
    sqlite.update_s<&person::name>(person{"purecpp_b"}, "id=2");
    auto vec1 = sqlite.query_s<person>("id=?", 1);
    auto vec2 = sqlite.query_s<person>("id=?", 2);
    CHECK(vec1.size() == 1);
    CHECK(vec2.size() == 1);
    CHECK(vec1.front().name == "purecpp_a");
    CHECK(vec2.front().name == "purecpp_b");
    sqlite.update_s<&person::name, &person::age>(person{"purecpp", 100, 1});
    sqlite.update_s<&person::name, &person::age>(person{"purecpp", 200},
                                                 "id=2");
    auto vec = sqlite.query_s<person>();
    vec1 = sqlite.query_s<person>("id=?", 1);
    vec2 = sqlite.query_s<person>("id=?", 2);
    CHECK(vec1.size() == 1);
    CHECK(vec2.size() == 1);
    CHECK(vec1.front().age == 100);
    CHECK(vec2.front().age == 200);
    CHECK(vec1.front().name == "purecpp");
    CHECK(vec2.front().name == "purecpp");
  }
#endif
}