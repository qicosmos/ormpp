#include <cstdint>
#include <iostream>
#include <string>
#include <tuple>

#include "dbng.hpp"
#include "sqlite.hpp"

using namespace ormpp;

struct cxx26_address {
  [[= ormpp::auto_key]] std::int64_t address_id{};

  [[= ormpp::not_null]] std::string city;
};

struct cxx26_person {
  [[= ormpp::auto_key]] std::int64_t id{};

  [[= ormpp::not_null]][[= ormpp::unique]] std::string name;

  [[= ormpp::references<^^cxx26_address::address_id>{}]] std::int64_t
      address_id{};
};

constexpr auto address_schema =
    ormpp::get_column_schema_annotations<cxx26_address>();
static_assert(address_schema.size() == 2);
static_assert(address_schema[0].primary_key);
static_assert(address_schema[0].auto_increment);
static_assert(address_schema[1].not_null);
static_assert(ormpp::get_annotated_auto_key<cxx26_address>() == "address_id");
static_assert(ormpp::name<&cxx26_address::address_id>() == "address_id");
static_assert(ylt::reflection::index_of<&cxx26_address::address_id>() == 0);
static_assert(ylt::reflection::index_of<cxx26_address>("address_id") == 0);
static_assert(ormpp::get_field_name<decltype(&cxx26_address::address_id)>(
                  "cxx26_address::address_id") == "address_id");

constexpr auto person_schema =
    ormpp::get_column_schema_annotations<cxx26_person>();
static_assert(person_schema.size() == 3);
static_assert(person_schema[1].not_null);
static_assert(person_schema[1].unique);
static_assert(person_schema[2].foreign_key);
static_assert(person_schema[2].referenced_table == "cxx26_address");
static_assert(person_schema[2].referenced_column == "address_id");

int main() {
  dbng<sqlite> db;
  if (!db.connect(":memory:")) {
    std::cerr << "failed to open in-memory sqlite database\n";
    return 1;
  }

  if (!db.create_table<cxx26_address>().execute() ||
      !db.create_table<cxx26_person>().execute()) {
    std::cerr << "failed to create annotated tables\n";
    return 2;
  }

  const auto address_id =
      db.get_insert_id_after_insert(cxx26_address{0, "Shanghai"});
  if (address_id != 1) {
    std::cerr << "annotated auto increment key was not used\n";
    return 3;
  }
  const auto stored_address_id = static_cast<std::int64_t>(address_id);

  if (db.update_some<&cxx26_address::city>(
          cxx26_address{stored_address_id, "Hangzhou"}) != 1) {
    std::cerr << "failed to update an annotated entity by member pointer\n";
    return 4;
  }
  auto addresses = db.query_s<cxx26_address>("address_id=?", address_id);
  if (addresses.size() != 1 || addresses.front().city != "Hangzhou") {
    std::cerr << "annotated column alias was not used by update/query\n";
    return 5;
  }

  if (db.insert(cxx26_person{0, "Tom", stored_address_id}) != 1) {
    std::cerr << "failed to insert annotated entity\n";
    return 6;
  }

  auto people = db.query_s<cxx26_person>("name=?", "Tom");
  if (people.size() != 1 || people.front().id != 1 ||
      people.front().address_id != stored_address_id) {
    std::cerr << "failed to query annotated entity\n";
    return 7;
  }

  auto address_columns = db.query_s<std::tuple<std::string, int, int>>(
      "SELECT name, `notnull`, pk FROM pragma_table_info('cxx26_address') "
      "ORDER BY cid");
  if (address_columns.size() != 2 ||
      address_columns[0] !=
          std::tuple<std::string, int, int>{"address_id", 0, 1} ||
      address_columns[1] != std::tuple<std::string, int, int>{"city", 1, 0}) {
    std::cerr << "annotated primary key or not-null constraint is missing\n";
    return 8;
  }

  auto unique_indexes = db.query_s<std::tuple<int>>(
      "SELECT count(*) FROM pragma_index_list('cxx26_person') "
      "WHERE `unique`=1");
  if (unique_indexes.size() != 1 || std::get<0>(unique_indexes.front()) != 1) {
    std::cerr << "annotated unique constraint is missing\n";
    return 9;
  }

  auto foreign_keys =
      db.query_s<std::tuple<std::string, std::string, std::string>>(
          "SELECT `table`, `from`, `to` "
          "FROM pragma_foreign_key_list('cxx26_person')");
  if (foreign_keys.size() != 1 ||
      std::get<0>(foreign_keys.front()) != "cxx26_address" ||
      std::get<1>(foreign_keys.front()) != "address_id" ||
      std::get<2>(foreign_keys.front()) != "address_id") {
    std::cerr << "annotated foreign key was not generated\n";
    return 10;
  }

  return 0;
}
