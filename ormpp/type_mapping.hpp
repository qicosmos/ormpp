//
// Created by qiyu on 10/23/17.
//
#ifdef ORMPP_ENABLE_MYSQL
#include <mysql.h>
#endif
#ifdef ORMPP_ENABLE_SQLITE3
#include <sqlite3.h>
#endif
#include <string>
#include <string_view>
#include <vector>

#include "pg_types.h"

using namespace std::string_view_literals;

#ifndef EXAMPLE1_TYPE_MAPPING_HPP
#define EXAMPLE1_TYPE_MAPPING_HPP

namespace ormpp {

using blob = std::vector<char>;

template <class T>
struct identity {};

#ifdef ORMPP_ENABLE_MYSQL
namespace ormpp_mysql {
inline int type_to_id(identity<char>) noexcept { return MYSQL_TYPE_TINY; }
inline int type_to_id(identity<short>) noexcept { return MYSQL_TYPE_SHORT; }
inline int type_to_id(identity<int>) noexcept { return MYSQL_TYPE_LONG; }
inline int type_to_id(identity<float>) noexcept { return MYSQL_TYPE_FLOAT; }
inline int type_to_id(identity<double>) noexcept { return MYSQL_TYPE_DOUBLE; }
inline int type_to_id(identity<int8_t>) noexcept { return MYSQL_TYPE_TINY; }
inline int type_to_id(identity<int64_t>) noexcept {
  return MYSQL_TYPE_LONGLONG;
}
inline int type_to_id(identity<uint8_t>) noexcept { return MYSQL_TYPE_SHORT; }
inline int type_to_id(identity<uint16_t>) noexcept { return MYSQL_TYPE_LONG; }
inline int type_to_id(identity<uint32_t>) noexcept {
  return MYSQL_TYPE_LONGLONG;
}
inline int type_to_id(identity<uint64_t>) noexcept {
  return MYSQL_TYPE_LONGLONG;
}
inline int type_to_id(identity<std::string>) noexcept {
  return MYSQL_TYPE_VAR_STRING;
}

inline constexpr auto type_to_name(identity<bool>) noexcept {
  return "BOOLEAN"sv;
}
inline constexpr auto type_to_name(identity<char>) noexcept {
  return "TINYINT"sv;
}
inline constexpr auto type_to_name(identity<short>) noexcept {
  return "SMALLINT"sv;
}
inline constexpr auto type_to_name(identity<int>) noexcept {
  return "INTEGER"sv;
}
inline constexpr auto type_to_name(identity<float>) noexcept {
  return "FLOAT"sv;
}
inline constexpr auto type_to_name(identity<double>) noexcept {
  return "DOUBLE"sv;
}
inline constexpr auto type_to_name(identity<int8_t>) noexcept {
  return "TINYINT"sv;
}
inline constexpr auto type_to_name(identity<int64_t>) noexcept {
  return "BIGINT"sv;
}
inline constexpr auto type_to_name(identity<uint8_t>) noexcept {
  return "SMALLINT"sv;
}
inline constexpr auto type_to_name(identity<uint16_t>) noexcept {
  return "INTEGER"sv;
}
inline constexpr auto type_to_name(identity<uint32_t>) noexcept {
  return "BIGINT"sv;
}
inline constexpr auto type_to_name(identity<uint64_t>) noexcept {
  return "BIGINT"sv;
}
inline constexpr auto type_to_name(identity<blob>) noexcept { return "BLOB"sv; }
inline auto type_to_name(identity<std::string>) noexcept { return "TEXT"sv; }
template <size_t N>
inline auto type_to_name(identity<std::array<char, N>>) noexcept {
  std::string s = "varchar(" + std::to_string(N) + ")";
  return s;
}
}  // namespace ormpp_mysql
#endif
#ifdef ORMPP_ENABLE_SQLITE3
namespace ormpp_sqlite {
inline constexpr auto type_to_name(identity<float>) noexcept {
  return "FLOAT"sv;
}
inline constexpr auto type_to_name(identity<double>) noexcept {
  return "DOUBLE"sv;
}
inline constexpr auto type_to_name(identity<bool>) noexcept {
  return "INTEGER"sv;
}
inline constexpr auto type_to_name(identity<char>) noexcept {
  return "INTEGER"sv;
}
inline constexpr auto type_to_name(identity<short>) noexcept {
  return "INTEGER"sv;
}
inline constexpr auto type_to_name(identity<int>) noexcept {
  return "INTEGER"sv;
}
inline constexpr auto type_to_name(identity<int8_t>) noexcept {
  return "INTEGER"sv;
}
inline constexpr auto type_to_name(identity<int64_t>) noexcept {
  return "INTEGER"sv;
}
inline constexpr auto type_to_name(identity<uint8_t>) noexcept {
  return "INTEGER"sv;
}
inline constexpr auto type_to_name(identity<uint16_t>) noexcept {
  return "INTEGER"sv;
}
inline constexpr auto type_to_name(identity<uint32_t>) noexcept {
  return "INTEGER"sv;
}
inline constexpr auto type_to_name(identity<uint64_t>) noexcept {
  return "INTEGER"sv;
}
inline constexpr auto type_to_name(identity<blob>) noexcept { return "BLOB"sv; }
inline auto type_to_name(identity<std::string>) noexcept { return "TEXT"sv; }
template <size_t N>
inline auto type_to_name(identity<std::array<char, N>>) noexcept {
  std::string s = "varchar(" + std::to_string(N) + ")";
  return s;
}
}  // namespace ormpp_sqlite
#endif
#ifdef ORMPP_ENABLE_PG
namespace ormpp_postgresql {
inline constexpr auto type_to_name(identity<bool>) noexcept {
  return "integer"sv;
}
inline constexpr auto type_to_name(identity<char>) noexcept { return "char"sv; }
inline constexpr auto type_to_name(identity<short>) noexcept {
  return "smallint"sv;
}
inline constexpr auto type_to_name(identity<int>) noexcept {
  return "integer"sv;
}
inline constexpr auto type_to_name(identity<float>) noexcept {
  return "real"sv;
}
inline constexpr auto type_to_name(identity<double>) noexcept {
  return "double precision"sv;
}
inline constexpr auto type_to_name(identity<int8_t>) noexcept {
  return "char"sv;
}
inline constexpr auto type_to_name(identity<int64_t>) noexcept {
  return "bigint"sv;
}
inline constexpr auto type_to_name(identity<uint8_t>) noexcept {
  return "smallint"sv;
}
inline constexpr auto type_to_name(identity<uint16_t>) noexcept {
  return "integer"sv;
}
inline constexpr auto type_to_name(identity<uint32_t>) noexcept {
  return "bigint"sv;
}
inline constexpr auto type_to_name(identity<uint64_t>) noexcept {
  return "bigint"sv;
}
inline constexpr auto type_to_name(identity<blob>) noexcept {
  return "bytea"sv;
}
inline auto type_to_name(identity<std::string>) noexcept { return "text"sv; }
template <size_t N>
inline auto type_to_name(identity<std::array<char, N>>) noexcept {
  std::string s = "varchar(" + std::to_string(N) + ")";
  return s;
}
}  // namespace ormpp_postgresql
#endif
}  // namespace ormpp

#endif  // EXAMPLE1_TYPE_MAPPING_HPP