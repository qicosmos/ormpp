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

int main() {
  dbng<mysql> mysql;

#ifdef ORMPP_ENABLE_MYSQL
  if (mysql.connect(ip, "root", password, "world")) {
    std::cout << "connect success" << std::endl;
  }
  else {
    std::cout << "connect fail" << std::endl;
  }
#endif

  return 0;
}