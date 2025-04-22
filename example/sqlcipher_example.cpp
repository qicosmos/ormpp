#include "ormpp/dbng.hpp"
#include "ormpp/mysql.hpp"
#include "ormpp/sqlite.hpp"
#include "ormpp/postgresql.hpp"
#include "ormpp/utilts.hpp"

#include <iostream>

using namespace ormpp;
using namespace std::string_literals;

struct person {
  int id;
  std::string name;
  int age;
};
REFLECTION(person, id, name, age)

int main() {
#ifdef ORMPP_ENABLE_SQLCIPHER
  std::cout << "SQLCipher example - creating and accessing an encrypted database\n";

  // Create a connection with encryption
  sqlite sqlite_db{};
  
  // Connect with an encryption key
  if (!sqlite_db.connect("encrypted_test.db", "my_encryption_key")) {
    std::cout << "Failed to connect to database: " << sqlite_db.get_last_error() << std::endl;
    return -1;
  }
  
  std::cout << "Connected to encrypted database successfully\n";
  
  // Create a table
  if (sqlite_db.create_datatable<person>()) {
    std::cout << "Created person table successfully\n";
  } else {
    std::cout << "Failed to create table: " << sqlite_db.get_last_error() << std::endl;
    return -1;
  }
  
  // Insert data
  person p{1, "Alice", 30};
  if (sqlite_db.insert(p) > 0) {
    std::cout << "Inserted record successfully\n";
  } else {
    std::cout << "Failed to insert record: " << sqlite_db.get_last_error() << std::endl;
  }
  
  // Query data
  auto result = sqlite_db.query<person>();
  std::cout << "Found " << result.size() << " records\n";
  for (const auto& p : result) {
    std::cout << "ID: " << p.id << ", Name: " << p.name << ", Age: " << p.age << std::endl;
  }
  
  // Change encryption key
  if (sqlite_db.change_encryption_key("new_encryption_key")) {
    std::cout << "Changed encryption key successfully\n";
  } else {
    std::cout << "Failed to change encryption key: " << sqlite_db.get_last_error() << std::endl;
  }
  
  // Verify database integrity
  if (sqlite_db.verify_encryption_integrity()) {
    std::cout << "Database integrity verified\n";
  } else {
    std::cout << "Database integrity check failed: " << sqlite_db.get_last_error() << std::endl;
  }
  
  // Set cipher configuration
  if (sqlite_db.set_cipher_config("cipher_page_size", "4096")) {
    std::cout << "Changed cipher page size successfully\n";
  } else {
    std::cout << "Failed to change cipher page size: " << sqlite_db.get_last_error() << std::endl;
  }
  
  // Remove encryption
  if (sqlite_db.remove_encryption()) {
    std::cout << "Removed encryption successfully\n";
  } else {
    std::cout << "Failed to remove encryption: " << sqlite_db.get_last_error() << std::endl;
  }
  
  std::cout << "SQLCipher example completed\n";
#else
  std::cout << "SQLCipher support is not enabled. Compile with -DENABLE_SQLCIPHER=ON\n";
#endif

  return 0;
} 