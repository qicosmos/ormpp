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

#include "../include/dbng.hpp"

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
  int id;
  std::string name;
  int age;
};
REFLECTION_WITH_NAME(student, "t_student", id, name, age)

int main() {
#ifdef ORMPP_ENABLE_MYSQL
  dbng<mysql> mysql;
  if (mysql.connect(ip, "root", password, db)) {
    std::cout << "connect success" << std::endl;
  }
  else {
    std::cout << "connect fail" << std::endl;
  }
#endif

#ifdef ORMPP_ENABLE_SQLITE3
  dbng<sqlite> sqlite;
  sqlite.connect(db);
  sqlite.create_datatable<person>(ormpp_auto_key{"id"});
  sqlite.create_datatable<student>(ormpp_auto_key{"id"});
#endif

  return 0;
}