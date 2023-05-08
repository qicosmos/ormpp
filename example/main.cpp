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
const char *db = "test_ormppdb";

struct person {
  int id;
  std::string name;
  std::optional<int> age;
  std::optional<std::string> phone;
};
REFLECTION(person, id, name, age, phone)

struct student {
  int id;
  std::string name;
  int age;
};
REFLECTION_WITH_NAME(student, "t_student", id, name, age)

int main() {
#ifdef ORMPP_ENABLE_MYSQL
  {
    dbng<mysql> mysql;
    if (mysql.connect(ip, "root", password, db)) {
      mysql.create_datatable<person>(ormpp_auto_key{"id"});
      mysql.delete_records<person>();
      mysql.insert<person>({0, "purecpp"});
      mysql.insert<person>({0, "purecpp", 6});
      mysql.insert<person>({0, "purecpp", 6, "123456"});
    }
    else {
      std::cout << "connect fail" << std::endl;
    }
  }

  {
    connection_pool<dbng<mysql>>::instance().init(4, ip, "root", password, db,
                                                  5, 3306);

    auto conn = connection_pool<dbng<mysql>>::instance().get();
    conn_guard guard(conn);
    conn->create_datatable<student>(ormpp_auto_key{"id"});
    conn->insert(student{1, "2", 3});
    auto val = conn->query<student>();
  }

#endif

#ifdef ORMPP_ENABLE_SQLITE3
  dbng<sqlite> sqlite;
  sqlite.connect(db);
  sqlite.create_datatable<person>(ormpp_auto_key{"id"});
  sqlite.create_datatable<student>(ormpp_auto_key{"id"});

  sqlite.insert<person>({0, "purecpp"});
  sqlite.insert<person>({0, "purecpp", 6});
  sqlite.insert<person>({0, "purecpp", 6, "123456"});
#endif

  return 0;
}