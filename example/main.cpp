#ifdef ORMPP_ENABLE_MYSQL
#include "mysql.hpp"
#endif

#ifdef ORMPP_ENABLE_SQLITE3
#include "sqlite.hpp"
#endif

#ifdef ORMPP_ENABLE_PG
#include "postgresql.hpp"
#endif

#include <iostream>

#include "connection_pool.hpp"
#include "dbng.hpp"

using namespace ormpp;
const char *password = "";
const char *ip = "127.0.0.1";
const char *username = "root";
const char *db = "test_ormppdb";

struct person {
  std::optional<std::string> name;
  std::optional<int> age;
  int id;
};
REGISTER_AUTO_KEY(person, id)
YLT_REFL(person, id, name, age)

struct student {
  std::string name;
  int age;
  int id;
  static constexpr std::string_view get_alias_struct_name(student *) {
    return "t_student";
  }
};
REGISTER_AUTO_KEY(student, id)
YLT_REFL(student, id, name, age)

int main() {
#ifdef ORMPP_ENABLE_MYSQL
  {
    dbng<mysql> mysql;
    if (mysql.connect(ip, username, password, db)) {
      mysql.create_datatable<person>(ormpp_auto_key{"id"});
      mysql.delete_records<person>();
      mysql.insert<person>({"purecpp"});
      mysql.insert<person>({"purecpp", 6});
    }
    else {
      std::cout << "connect fail" << std::endl;
    }
  }

  {
    connection_pool<dbng<mysql>>::instance().init(4, ip, username, password, db,
                                                  5, 3306);
    auto conn = connection_pool<dbng<mysql>>::instance().get();
    conn_guard guard(conn);
    conn->create_datatable<student>(ormpp_auto_key{"id"});
    auto vec = conn->query<student>();
  }
#endif

#ifdef ORMPP_ENABLE_SQLITE3
  dbng<sqlite> sqlite;
  sqlite.connect(db);
  sqlite.create_datatable<person>(ormpp_auto_key{"id"});
  sqlite.create_datatable<student>(ormpp_auto_key{"id"});

  {
    sqlite.delete_records<person>();
    sqlite.insert<person>({"purecpp", 1});
    sqlite.insert<person>({"purecpp", 2});
    auto vec = sqlite.query<person>();
    for (auto &[name, age, id] : vec) {
      std::cout << id << ", " << *name << ", " << *age << "\n";
    }
  }

  {
    sqlite.delete_records<student>();
    sqlite.insert<student>({"purecpp", 1});
    sqlite.insert<student>({"purecpp", 2});
    sqlite.insert<student>({"purecpp", 3});
    sqlite.insert<student>({"purecpp", 3});
    {
      auto vec = sqlite.query<student>("name='purecpp'", "order by age desc");
      for (auto &[name, age, id] : vec) {
        std::cout << id << ", " << name << ", " << age << "\n";
      }
    }
    {
      auto vec = sqlite.query<student>("age=3", "order by id desc", "limit 1");
      for (auto &[name, age, id] : vec) {
        std::cout << id << ", " << name << ", " << age << "\n";
      }
    }
  }
#endif

  return 0;
}