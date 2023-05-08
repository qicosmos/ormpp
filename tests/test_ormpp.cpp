#ifdef ORMPP_ENABLE_MYSQL
#include "mysql.hpp"
#endif

#ifdef ORMPP_ENABLE_SQLITE3
#include "sqlite.hpp"
#endif

#ifdef ORMPP_ENABLE_PG
#include "postgresql.hpp"
#endif

#include "connection_pool.hpp"
#include "dbng.hpp"
#include "doctest.h"
#include "ormpp_cfg.hpp"

using namespace std::string_literals;

using namespace ormpp;
const char *password = "";
const char *ip = "127.0.0.1";
const char *db = "test_ormppdb";

struct person {
  int id;
  std::string name;
  int age;
};
REFLECTION(person, id, name, age)

struct student {
  int code;  // key
  std::string name;
  char sex;
  int age;
  double dm;
  std::string classroom;
};
REFLECTION(student, code, name, sex, age, dm, classroom)

struct simple {
  int id;
  double code;
  int age;
};
REFLECTION(simple, id, code, age);

// TEST_CASE(mysql_performance){
//    dbng<mysql> mysql;
//
//    REQUIRE(mysql.connect(ip, "root", password, db));
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

struct test_order {
  int id;
  std::string name;
};
REFLECTION(test_order, name, id);

TEST_CASE("random_reflection_order") {
#ifdef ORMPP_ENABLE_MYSQL
  dbng<mysql> mysql;
  REQUIRE(mysql.connect(ip, "root", password, db,
                        /*timeout_seconds=*/5, 3306));
  REQUIRE(mysql.execute(
      "create table if not exists `test_order` (id int, name text);"));
  mysql.delete_records<test_order>();
  int id = 666;
  std::string name = "hello";
  mysql.insert(test_order{id, name});
  auto v = mysql.query<test_order>();
  REQUIRE(v.size() > 0);
  CHECK(v.front().id == id);
  CHECK(v.front().name == name);
#endif
}

struct custom_name {
  int id;
  std::string name;
};
REFLECTION_WITH_NAME(custom_name, "test_order", id, name);

TEST_CASE("orm_custom_name") {
#ifdef ORMPP_ENABLE_MYSQL
  dbng<mysql> mysql;
  REQUIRE(mysql.connect(ip, "root", password, db));
  auto v = mysql.query<custom_name>();
  CHECK(v.size() > 0);
  {
    auto v = mysql.query(FID(custom_name::name), "=", "hello");
    CHECK(v.size() > 0);
  }
#endif
}

struct dummy {
  int id;
  std::string name;
};
REFLECTION(dummy, id, name);

// TEST_CASE("mysql_exist_tb") {
//   dbng<mysql> mysql;
//   REQUIRE(mysql.connect(ip, "root", password, db,
//                         /*timeout_seconds=*/5, 3306));
//   dummy d{0, "tom"};
//   dummy d1{0, "jerry"};
//   mysql.insert(d);
//   mysql.insert(d1);
//   auto v = mysql.query<dummy>("limit 1, 1");
//   std::cout << v.size() << "\n";
// }

TEST_CASE("mysql_pool") {
  //	dbng<sqlite> sqlite;
  //	sqlite.connect(db);
  //	sqlite.create_datatable<test_tb>(ormpp_unique{ "name" });
  //	test_tb tb{ 1, "aa" };
  //	sqlite.insert(tb);
  //	auto vt = sqlite.query<test_tb>();
  //	auto vt1 = sqlite.query<std::tuple<test_tb>>("select * from test_tb");
  //    auto& pool = connection_pool<dbng<mysql>>::instance();
  //    try {
  //        pool.init(1, ip, "root", password, db, 2);
  //    }catch(const std::exception& e){
  //        std::cout<<e.what()<<std::endl;
  //        return;
  //    }
  //	auto con = pool.get();
  //	auto v = con->query<std::tuple<test_tb>>("select * from test_tb");
  //	con->create_datatable<test_tb>(ormpp_unique{"name"});
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
}

TEST_CASE("test_ormpp_cfg") {
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
  auto result1 = conn1->query<student>();
  std::cout << result1.size() << std::endl;
#endif
}

#ifdef ORMPP_ENABLE_PG
TEST_CASE("postgres_pool") {
  auto &pool = connection_pool<dbng<postgresql>>::instance();
  try {
    pool.init(3, ip, "root", password, db, 2);
    pool.init(7, ip, "root", password, db, 2);
  } catch (const std::exception &e) {
    std::cout << e.what() << std::endl;
    return;
  }

  auto conn1 = pool.get();
  auto conn2 = pool.get();
  auto conn3 = pool.get();

  std::thread thd([conn2, &pool] {
    std::this_thread::sleep_for(std::chrono::seconds(15));
    pool.return_back(conn2);
  });

  auto conn4 = pool.get();  // 10s later, timeout
  CHECK(conn4 == nullptr);
  auto conn5 = pool.get();
  CHECK(conn5 != nullptr);

  thd.join();

  for (int i = 0; i < 10; ++i) {
    auto conn = pool.get();
    // conn_guard guard(conn);
    if (conn == nullptr) {
      std::cout << "no available conneciton" << std::endl;
      continue;
    }

    bool r = conn->create_datatable<person>();
  }
}
#endif

TEST_CASE("orm_connect") {
  int timeout = 5;

#ifdef ORMPP_ENABLE_PG
  dbng<postgresql> postgres;
  REQUIRE(postgres.connect(ip, "root", password, db));
  REQUIRE(postgres.disconnect());
  REQUIRE(postgres.connect(ip, "root", password, db, timeout));
#endif

#ifdef ORMPP_ENABLE_MYSQL
  dbng<mysql> mysql;
  REQUIRE(mysql.connect(ip, "root", password, db));
  REQUIRE(mysql.disconnect());
  REQUIRE(mysql.connect(ip, "root", password, db, timeout));
#endif

#ifdef ORMPP_ENABLE_SQLITE3
  dbng<sqlite> sqlite;
  REQUIRE(sqlite.connect(db));
  REQUIRE(sqlite.disconnect());
#endif
}

TEST_CASE("orm_create_table") {
  ormpp_key key{"id"};
  ormpp_not_null not_null{{"id", "age"}};
  ormpp_auto_key auto_key{"id"};

#ifdef ORMPP_ENABLE_PG
  dbng<postgresql> postgres;
  REQUIRE(postgres.connect(ip, "root", password, db));
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
  REQUIRE(mysql.connect(ip, "root", password, db));
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

TEST_CASE("orm_insert_query") {
  ormpp_key key{"code"};
  ormpp_not_null not_null{{"code", "age"}};
  ormpp_auto_key auto_key{"code"};

  student s = {0, "tom", 0, 19, 1.5, "room2"};
  student s1 = {0, "jack", 1, 20, 2.5, "room3"};
  student s2 = {0, "mke", 2, 21, 3.5, "room4"};
  std::vector<student> v{s1, s2};

#ifdef ORMPP_ENABLE_MYSQL
  dbng<mysql> mysql;
  REQUIRE(mysql.connect(ip, "root", password, db));
  auto vv0 = mysql.query(FID(simple::id), "<", "5");
  auto vv = mysql.query(FID(simple::id), "<", 5);
  auto vv3 = mysql.query(FID(person::name), "<", "5");
  auto vv5 = mysql.query(FID(person::name), "<", 5);
  mysql.delete_records(FID(simple::id), "=", 3);
#endif

#ifdef ORMPP_ENABLE_PG
  dbng<postgresql> postgres;
  REQUIRE(postgres.connect(ip, "root", password, db));
  auto vv1 = postgres.query(FID(simple::id), "<", "5");
#endif

#ifdef ORMPP_ENABLE_SQLITE3
  dbng<sqlite> sqlite;
  REQUIRE(sqlite.connect(db));
  auto vv2 = sqlite.query(FID(simple::id), "<", "5");
#endif

  // auto key
  {
#ifdef ORMPP_ENABLE_PG
    REQUIRE(postgres.create_datatable<student>(auto_key, not_null));
    CHECK(postgres.insert(s) == 1);
    auto result2 = postgres.query<student>();
    CHECK(result2.size() == 1);
    CHECK(postgres.insert(v) == 2);
#endif

#ifdef ORMPP_ENABLE_SQLITE3
    REQUIRE(sqlite.create_datatable<student>(auto_key));
    CHECK(sqlite.delete_records<student>());
    CHECK(sqlite.insert(s) == 1);
    auto result3 = sqlite.query<student>();
    CHECK(result3.size() == 1);
    CHECK(sqlite.insert(v) == 2);
    auto result6 = sqlite.query<student>();
    CHECK(result6.size() == 3);
    auto v2 = sqlite.query(FID(student::code), "<", "5");
    auto v3 = sqlite.query<student>("limit 2");
#endif
  }

  // key
  {
#ifdef ORMPP_ENABLE_MYSQL
    REQUIRE(mysql.create_datatable<student>(auto_key, not_null));
    CHECK(mysql.delete_records<student>());
    CHECK(mysql.insert(s) == 1);
    auto result1 = mysql.query<student>("limit 3");
    CHECK(result1.size() == 1);
    CHECK(mysql.insert(s) == 1);
    CHECK(mysql.insert(v) == 2);
    auto result4 = mysql.query<student>();
    CHECK(result4.size() == 4);
    auto result5 = mysql.query<student>();
    CHECK(result5.size() == 4);

    REQUIRE(mysql.create_datatable<student>(key, not_null));
    v[0].code = 1;
    v[1].code = 2;
    CHECK(mysql.insert(s) == 1);
    auto result11 = mysql.query<student>();
    CHECK(result11.size() == 5);
    CHECK(mysql.delete_records<student>());
    CHECK(mysql.insert(v) == 2);
    auto result44 = mysql.query<student>();
    CHECK(result44.size() == 2);
    auto result55 = mysql.query<student>();
    CHECK(result55.size() == 2);
    auto result6 = mysql.query<student>();
    CHECK(result6.size() == 2);
#endif

#ifdef ORMPP_ENABLE_PG
    REQUIRE(postgres.create_datatable<student>(key, not_null));
    CHECK(postgres.insert(s) == 1);
    auto result2 = postgres.query<student>();
    CHECK(result2.size() == 1);
    CHECK(postgres.delete_records<student>());
    CHECK(postgres.insert(v) == 2);
#endif

#ifdef ORMPP_ENABLE_SQLITE3
    REQUIRE(sqlite.create_datatable<student>(auto_key));
    CHECK(sqlite.insert(s) == 1);
    auto result3 = sqlite.query<student>();
    CHECK(result3.size() == 4);
    CHECK(sqlite.delete_records<student>());
    CHECK(sqlite.insert(v) == 2);
#endif
  }
}

TEST_CASE("orm_update") {
  ormpp_key key{"code"};
  ormpp_not_null not_null{{"code", "age"}};
  ormpp_auto_key auto_key{"code"};

  student s = {1, "tom", 0, 19, 1.5, "room2"};
  student s1 = {2, "jack", 1, 20, 2.5, "room3"};
  student s2 = {3, "mke", 2, 21, 3.5, "room4"};
  std::vector<student> v{s, s1, s2};

#ifdef ORMPP_ENABLE_MYSQL
  dbng<mysql> mysql;

  REQUIRE(mysql.connect(ip, "root", password, db));
  CHECK(mysql.delete_records<student>());
  REQUIRE(mysql.create_datatable<student>(key, not_null));
  CHECK(mysql.insert(v) == 3);

  v[0].name = "test1";
  v[1].name = "test2";

  CHECK(mysql.update(v[0]) == 1);
  auto result = mysql.query<student>();
  CHECK(mysql.update(v[1]) == 1);
  auto result1 = mysql.query<student>();
#endif

#ifdef ORMPP_ENABLE_PG
  dbng<postgresql> postgres;
  REQUIRE(postgres.connect(ip, "root", password, db));
  REQUIRE(postgres.create_datatable<student>(key, not_null));
  CHECK(postgres.insert(v) == 3);
  CHECK(postgres.update(v[0]) == 1);
  auto result2 = postgres.query<student>();
  CHECK(postgres.update(v[1]) == 1);
  auto result3 = postgres.query<student>();
#endif

#ifdef ORMPP_ENABLE_SQLITE3
  dbng<sqlite> sqlite;
  REQUIRE(sqlite.connect(db));
  CHECK(sqlite.delete_records<student>());
  REQUIRE(sqlite.create_datatable<student>(key));
  CHECK(sqlite.insert(v) == 3);
  CHECK(sqlite.update(v[0]) == 1);
  auto result4 = sqlite.query<student>();
  CHECK(sqlite.update(v[1]) == 1);
  auto result5 = sqlite.query<student>();
#endif
}

TEST_CASE("orm_multi_update") {
  ormpp_key key{"code"};
  ormpp_not_null not_null{{"code", "age"}};
  ormpp_auto_key auto_key{"code"};

  student s = {1, "tom", 0, 19, 1.5, "room2"};
  student s1 = {2, "jack", 1, 20, 2.5, "room3"};
  student s2 = {3, "mike", 2, 21, 3.5, "room4"};
  std::vector<student> v{s, s1, s2};

#ifdef ORMPP_ENABLE_MYSQL
  dbng<mysql> mysql;
  REQUIRE(mysql.connect(ip, "root", password, db));
  REQUIRE(mysql.create_datatable<student>(key, not_null));
  CHECK(mysql.delete_records<student>());
  CHECK(mysql.insert(v) == 3);
  v[0].name = "test1";
  v[1].name = "test2";
  v[2].name = "test3";

  CHECK(mysql.update(v) == 3);
  auto result = mysql.query<student>();
  CHECK(result.size() == 3);
#endif

#ifdef ORMPP_ENABLE_SQLITE3
  dbng<sqlite> sqlite;
  REQUIRE(sqlite.connect(db));
  CHECK(sqlite.delete_records<student>());
  REQUIRE(sqlite.create_datatable<student>(key));
  CHECK(sqlite.insert(v) == 3);
  CHECK(sqlite.update(v) == 3);
  auto result4 = sqlite.query<student>();
  CHECK(result4.size() == 3);
#endif

#ifdef ORMPP_ENABLE_PG
  dbng<postgresql> postgres;
  REQUIRE(postgres.connect(ip, "root", password, db));
  REQUIRE(postgres.create_datatable<student>(key, not_null));
  CHECK(postgres.insert(v) == 3);
  CHECK(postgres.update(v) == 3);
  auto result2 = postgres.query<student>();
  CHECK(result2.size() == 3);
#endif
}

TEST_CASE("orm_delete") {
  ormpp_key key{"code"};
  ormpp_not_null not_null{{"code", "age"}};
  ormpp_auto_key auto_key{"code"};

  student s = {1, "tom", 0, 19, 1.5, "room2"};
  student s1 = {2, "jack", 1, 20, 2.5, "room3"};
  student s2 = {3, "mike", 2, 21, 3.5, "room4"};
  std::vector<student> v{s, s1, s2};

#ifdef ORMPP_ENABLE_MYSQL
  dbng<mysql> mysql;
  REQUIRE(mysql.connect(ip, "root", password, db));
  mysql.delete_records<student>();
  REQUIRE(mysql.create_datatable<student>(key, not_null));
  CHECK(mysql.insert(v) == 3);
  REQUIRE(mysql.delete_records<student>("code=1"));
  CHECK(mysql.query<student>().size() == 2);
  REQUIRE(mysql.delete_records<student>(""));
  auto result = mysql.query<student>();
  CHECK(result.size() == 0);
#endif

#ifdef ORMPP_ENABLE_PG
  dbng<postgresql> postgres;
  REQUIRE(postgres.connect(ip, "root", password, db));
  REQUIRE(postgres.create_datatable<student>(key, not_null));
  CHECK(postgres.insert(v) == 3);
  REQUIRE(postgres.delete_records<student>("code=1"));
  CHECK(postgres.query<student>().size() == 2);
  REQUIRE(postgres.delete_records<student>(""));
  auto result1 = postgres.query<student>();
  CHECK(result1.size() == 0);
#endif

#ifdef ORMPP_ENABLE_SQLITE3
  dbng<sqlite> sqlite;
  REQUIRE(sqlite.connect(db));
  REQUIRE(sqlite.create_datatable<student>(key));
  sqlite.delete_records<student>();
  CHECK(sqlite.insert(v) == 3);
  REQUIRE(sqlite.delete_records<student>("code=1"));
  CHECK(sqlite.query<student>().size() == 2);
  REQUIRE(sqlite.delete_records<student>(""));
  auto result2 = sqlite.query<student>();
  CHECK(result2.size() == 0);
#endif
}

TEST_CASE("orm_query") {
  ormpp_key key{"id"};
  simple s1 = {1, 2.5, 3};
  simple s2 = {2, 3.5, 4};
  simple s3 = {3, 4.5, 5};
  std::vector<simple> v{s1, s2, s3};

#ifdef ORMPP_ENABLE_MYSQL
  dbng<mysql> mysql;
  REQUIRE(mysql.connect(ip, "root", password, db));
  mysql.delete_records<simple>();
  REQUIRE(mysql.create_datatable<simple>(key));
  CHECK(mysql.insert(v) == 3);
  auto result = mysql.query<simple>();
  CHECK(result.size() == 3);
  auto result3 = mysql.query<simple>("id=1");
  CHECK(result3.size() == 1);
#endif

#ifdef ORMPP_ENABLE_SQLITE3
  dbng<sqlite> sqlite;
  REQUIRE(sqlite.connect(db));
  REQUIRE(sqlite.create_datatable<simple>(key));
  sqlite.delete_records<simple>();
  CHECK(sqlite.insert(v) == 3);
  auto result2 = sqlite.query<simple>();
  CHECK(result2.size() == 3);
  auto result5 = sqlite.query<simple>("id=3");
  CHECK(result5.size() == 1);
#endif

#ifdef ORMPP_ENABLE_PG
  dbng<postgresql> postgres;
  REQUIRE(postgres.connect(ip, "root", password, db));
  REQUIRE(postgres.create_datatable<simple>(key));
  CHECK(postgres.insert(v) == 3);
  auto result1 = postgres.query<simple>();
  CHECK(result1.size() == 3);
  auto result4 = postgres.query<simple>("where id=2");
  CHECK(result4.size() == 1);
#endif
}

TEST_CASE("orm_query_some") {
  ormpp_key key{"code"};
  ormpp_not_null not_null{{"code", "age"}};
  ormpp_auto_key auto_key{"code"};

  student s = {1, "tom", 0, 19, 1.5, "room2"};
  student s1 = {2, "jack", 1, 20, 2.5, "room3"};
  student s2 = {3, "mike", 2, 21, 3.5, "room4"};
  std::vector<student> v{s, s1, s2};

#ifdef ORMPP_ENABLE_MYSQL
  dbng<mysql> mysql;
  REQUIRE(mysql.connect(ip, "root", password, db));
  mysql.delete_records<student>();
  REQUIRE(mysql.create_datatable<student>(key, not_null));
  CHECK(mysql.insert(v) == 3);
  auto result3 = mysql.query<std::tuple<int>>("select count(1) from student");
  CHECK(result3.size() == 1);
  CHECK(std::get<0>(result3[0]) == 3);

  auto result4 = mysql.query<std::tuple<int>>("select count(1) from student");
  CHECK(result4.size() == 1);
  CHECK(std::get<0>(result4[0]) == 3);

  auto result5 = mysql.query<std::tuple<int>>("select count(1) from student");
  CHECK(result5.size() == 1);
  CHECK(std::get<0>(result5[0]) == 3);
  auto result = mysql.query<std::tuple<int, std::string, double>>(
      "select code, name, dm from student");
  CHECK(result.size() == 3);
#endif

#ifdef ORMPP_ENABLE_PG
  dbng<postgresql> postgres;
  REQUIRE(postgres.connect(ip, "root", password, db));
  REQUIRE(postgres.create_datatable<student>(key, not_null));
  CHECK(postgres.insert(v) == 3);
  auto result1 = postgres.query<std::tuple<int, std::string, double>>(
      "select code, name, dm from student");
  CHECK(result1.size() == 3);
#endif

#ifdef ORMPP_ENABLE_SQLITE3
  dbng<sqlite> sqlite;
  REQUIRE(sqlite.connect(db));
  CHECK(sqlite.insert(v) == 3);
  REQUIRE(sqlite.create_datatable<student>(key));
  auto result2 = sqlite.query<std::tuple<int, std::string, double>>(
      "select code, name, dm from student");
  CHECK(result2.size() == 3);
#endif
}

TEST_CASE("orm_query_multi_table") {
  ormpp_key key{"code"};
  ormpp_not_null not_null{{"code", "age"}};
  ormpp_auto_key auto_key{"code"};

  student s = {1, "tom", 0, 19, 1.5, "room2"};
  student s1 = {2, "jack", 1, 20, 2.5, "room3"};
  student s2 = {3, "mike", 2, 21, 3.5, "room4"};
  std::vector<student> v{s, s1, s2};

  ormpp_key key1{"id"};
  person p = {1, "test1", 2};
  person p1 = {2, "test2", 3};
  person p2 = {3, "test3", 4};
  std::vector<person> v1{p, p1, p2};

#ifdef ORMPP_ENABLE_MYSQL
  dbng<mysql> mysql;
  REQUIRE(mysql.connect(ip, "root", password, db));
  mysql.delete_records<student>();
  mysql.delete_records<person>();
  REQUIRE(mysql.create_datatable<student>(key, not_null));
  CHECK(mysql.insert(v) == 3);
  REQUIRE(mysql.create_datatable<person>(key1, not_null));
  CHECK(mysql.insert(v1) == 3);
  auto result = mysql.query<std::tuple<person, std::string, int>>(
      "select person.*, student.name, student.age from person, student"s);
  CHECK(result.size() == 9);
  auto result3 = mysql.query<std::tuple<person, student>>(
      "select * from person, student"s);
  CHECK(result.size() == 9);
#endif

#ifdef ORMPP_ENABLE_PG
  dbng<postgresql> postgres;
  REQUIRE(postgres.connect(ip, "root", password, db));
  REQUIRE(postgres.create_datatable<student>(key, not_null));
  CHECK(postgres.insert(v) == 3);
  REQUIRE(postgres.create_datatable<person>(key1, not_null));
  CHECK(postgres.insert(v1) == 3);
  CHECK(sqlite.insert(v1) == 3);
  auto result1 = postgres.query<std::tuple<int, std::string, double>>(
      "select person.*, student.name, student.age from person, student"s);
  CHECK(result1.size() == 9);
  auto result4 = postgres.query<std::tuple<person, student>>(
      "select * from person, student"s);
  CHECK(result1.size() == 9);
#endif

#ifdef ORMPP_ENABLE_SQLITE3
  dbng<sqlite> sqlite;
  REQUIRE(sqlite.connect(db));
  sqlite.delete_records<student>();
  sqlite.delete_records<person>();
  REQUIRE(sqlite.create_datatable<student>(key));
  CHECK(sqlite.insert(v) == 3);
  REQUIRE(sqlite.create_datatable<person>(key1));
  CHECK(sqlite.insert(v1) == 3);
  auto result2 = sqlite.query<std::tuple<int, std::string, double>>(
      "select person.*, student.name, student.age from person, student"s);
  CHECK(result2.size() == 9);
  auto result5 = sqlite.query<std::tuple<person, student>>(
      "select * from person, student"s);
  CHECK(result2.size() == 9);
#endif
}

TEST_CASE("orm_transaction") {
  ormpp_key key{"code"};
  ormpp_not_null not_null{{"code", "age"}};
  ormpp_auto_key auto_key{"code"};

  student s = {1, "tom", 0, 19, 1.5, "room2"};
  student s1 = {2, "jack", 1, 20, 2.5, "room3"};
  student s2 = {3, "mike", 2, 21, 3.5, "room4"};
  std::vector<student> v{s, s1, s2};

#ifdef ORMPP_ENABLE_MYSQL
  dbng<mysql> mysql;
  REQUIRE(mysql.connect(ip, "root", password, db));
  mysql.delete_records<student>();
  REQUIRE(mysql.create_datatable<student>(key, not_null));

  REQUIRE(mysql.begin());
  for (int i = 0; i < 10; ++i) {
    student st = {i, "tom", 0, 19, 1.5, "room2"};
    if (!mysql.insert(st)) {
      mysql.rollback();
      return;
    }
  }
  REQUIRE(mysql.commit());
  auto result = mysql.query<student>();
  // CHECK(result.size() == 10);
#endif

#ifdef ORMPP_ENABLE_PG
  dbng<postgresql> postgres;
  REQUIRE(postgres.connect(ip, "root", password, db));
  REQUIRE(postgres.create_datatable<student>(key, not_null));
  REQUIRE(postgres.begin());
  for (int i = 0; i < 10; ++i) {
    student s = {i, "tom", 0, 19, 1.5, "room2"};
    if (!postgres.insert(s)) {
      postgres.rollback();
      return;
    }
  }
  REQUIRE(postgres.commit());
  auto result1 = postgres.query<student>();
  CHECK(result1.size() == 10);
#endif

#ifdef ORMPP_ENABLE_SQLITE3
  dbng<sqlite> sqlite;
  REQUIRE(sqlite.connect(db));
  REQUIRE(sqlite.create_datatable<student>(key));
  REQUIRE(sqlite.begin());
  for (int i = 0; i < 10; ++i) {
    student st = {i, "tom", 0, 19, 1.5, "room2"};
    if (!sqlite.insert(st)) {
      sqlite.rollback();
      return;
    }
  }
  REQUIRE(sqlite.commit());
  auto result2 = sqlite.query<student>();
  CHECK(result2.size() == 10);
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

TEST_CASE("orm_aop") {
  // dbng<mysql> mysql;
  // auto r = mysql.wraper_connect<log, validate>("127.0.0.1", "root", password,
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

#ifdef ORMPP_ENABLE_MYSQL
struct image {
  int id;
  ormpp::blob bin;
};

REFLECTION(image, id, bin);

TEST_CASE("orm_mysql_blob") {
  dbng<mysql> mysql;

  REQUIRE(mysql.connect("127.0.0.1", "root", password, db));
  REQUIRE(mysql.execute("DROP TABLE IF EXISTS image"));

  REQUIRE(mysql.create_datatable<image>());

  auto data = "this is a test binary stream\0, and ?...";
  auto size = 40;

  image img;
  img.id = 1;
  img.bin.assign(data, data + size);

  REQUIRE(mysql.insert(img) == 1);

  auto result = mysql.query<image>("id=1");
  REQUIRE(result.size() == 1);
  REQUIRE(result[0].bin.size() == size);
}

struct image_ex {
  int id;
  ormpp::blob bin;
  std::string time;
};

REFLECTION(image_ex, id, bin, time);

TEST_CASE("orm_mysql_blob_tuple") {
  dbng<mysql> mysql;

  REQUIRE(mysql.connect("127.0.0.1", "root", password, db));
  REQUIRE(mysql.execute("DROP TABLE IF EXISTS image_ex"));

  REQUIRE(mysql.create_datatable<image_ex>());

  auto data = "this is a test binary stream\0, and ?...";
  auto size = 40;

  image_ex img_ex;
  img_ex.id = 1;
  img_ex.bin.assign(data, data + size);
  img_ex.time = "2023-03-29 13:55:00";

  REQUIRE(mysql.insert(img_ex) == 1);

  auto result = mysql.query<image_ex>("id=1");
  REQUIRE(result.size() == 1);
  REQUIRE(result[0].bin.size() == size);

  using image_t = std::tuple<image, std::string>;
  auto ex_results =
      mysql.query<image_t>("select id,bin,time from image_ex where id=1;");
  REQUIRE(ex_results.size() == 1);

  auto &img = std::get<0>(ex_results[0]);
  auto &time = std::get<1>(ex_results[0]);

  REQUIRE(img.id == 1);
  REQUIRE(img.bin.size() == size);
  REQUIRE(time == img_ex.time);
}
#endif