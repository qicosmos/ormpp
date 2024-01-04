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
template <class T>
struct identity {};

#define REGISTER_TYPE(Type, Index)                                           \
  inline constexpr int type_to_id(identity<Type>) noexcept { return Index; } \
  inline constexpr auto id_to_type(                                          \
      std::integral_constant<std::size_t, Index>) noexcept {                 \
    Type res{};                                                              \
    return res;                                                              \
  }

#ifdef ORMPP_ENABLE_MYSQL
namespace ormpp_mysql {
REGISTER_TYPE(char, MYSQL_TYPE_TINY)
REGISTER_TYPE(short, MYSQL_TYPE_SHORT)
REGISTER_TYPE(int, MYSQL_TYPE_LONG)
REGISTER_TYPE(float, MYSQL_TYPE_FLOAT)
REGISTER_TYPE(double, MYSQL_TYPE_DOUBLE)
REGISTER_TYPE(int64_t, MYSQL_TYPE_LONGLONG)

using blob = std::vector<char>;

inline int type_to_id(identity<std::string>) noexcept {
  return MYSQL_TYPE_VAR_STRING;
}
inline std::string id_to_type(
    std::integral_constant<std::size_t, MYSQL_TYPE_VAR_STRING>) noexcept {
  std::string res{};
  return res;
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
inline constexpr auto type_to_name(identity<int64_t>) noexcept {
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
REGISTER_TYPE(int, SQLITE_INTEGER)
REGISTER_TYPE(double, SQLITE_FLOAT)

inline int type_to_id(identity<std::string>) noexcept { return SQLITE_TEXT; }
inline std::string id_to_type(
    std::integral_constant<std::size_t, SQLITE_TEXT>) noexcept {
  std::string res{};
  return res;
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
inline constexpr auto type_to_name(identity<float>) noexcept {
  return "FLOAT"sv;
}
inline constexpr auto type_to_name(identity<double>) noexcept {
  return "DOUBLE"sv;
}
inline constexpr auto type_to_name(identity<int64_t>) noexcept {
  return "INTEGER"sv;
}
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
REGISTER_TYPE(bool, BOOLOID)
REGISTER_TYPE(char, CHAROID)
REGISTER_TYPE(short, INT2OID)
REGISTER_TYPE(int, INT4OID)
REGISTER_TYPE(float, FLOAT4OID)
REGISTER_TYPE(double, FLOAT8OID)
REGISTER_TYPE(int64_t, INT8OID)

inline int type_to_id(identity<std::string>) noexcept { return TEXTOID; }
inline std::string id_to_type(
    std::integral_constant<std::size_t, TEXTOID>) noexcept {
  std::string res{};
  return res;
}

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
inline constexpr auto type_to_name(identity<int64_t>) noexcept {
  return "bigint"sv;
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