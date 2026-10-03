//
// Created by qiyu on 10/23/17.
//
#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "pg_types.h"

using namespace std::string_view_literals;

#ifndef EXAMPLE1_TYPE_MAPPING_HPP
#define EXAMPLE1_TYPE_MAPPING_HPP

namespace ormpp {

using blob = std::vector<char>;

struct date {
  std::string value;

  date() = default;
  date(const char *v) : value(v) {}
  date(std::string v) : value(std::move(v)) {}

  const char *data() const noexcept { return value.data(); }
  std::size_t size() const noexcept { return value.size(); }
};

struct time {
  std::string value;

  time() = default;
  time(const char *v) : value(v) {}
  time(std::string v) : value(std::move(v)) {}

  const char *data() const noexcept { return value.data(); }
  std::size_t size() const noexcept { return value.size(); }
};

struct datetime {
  std::string value;

  datetime() = default;
  datetime(const char *v) : value(v) {}
  datetime(std::string v) : value(std::move(v)) {}

  const char *data() const noexcept { return value.data(); }
  std::size_t size() const noexcept { return value.size(); }
};

struct timestamp {
  std::string value;

  timestamp() = default;
  timestamp(const char *v) : value(v) {}
  timestamp(std::string v) : value(std::move(v)) {}

  const char *data() const noexcept { return value.data(); }
  std::size_t size() const noexcept { return value.size(); }
};

template <int Precision, int Scale>
struct decimal {
  static_assert(Precision > 0, "decimal precision must be greater than 0");
  static_assert(Scale >= 0, "decimal scale must be greater than or equal to 0");
  static_assert(Scale <= Precision,
                "decimal scale must be less than or equal to precision");

  static constexpr int precision = Precision;
  static constexpr int scale = Scale;

  std::string value;

  decimal() = default;
  decimal(const char *v) : value(v) {}
  decimal(std::string v) : value(std::move(v)) {}

  const char *data() const noexcept { return value.data(); }
  std::size_t size() const noexcept { return value.size(); }
};

template <typename T>
struct is_decimal_type : std::false_type {};

template <int Precision, int Scale>
struct is_decimal_type<decimal<Precision, Scale>> : std::true_type {};

template <typename T>
inline constexpr bool is_decimal_type_v =
    is_decimal_type<std::remove_cv_t<T>>::value;

template <typename T>
struct is_db_text_type : std::false_type {};

template <>
struct is_db_text_type<date> : std::true_type {};

template <>
struct is_db_text_type<time> : std::true_type {};

template <>
struct is_db_text_type<datetime> : std::true_type {};

template <>
struct is_db_text_type<timestamp> : std::true_type {};

template <int Precision, int Scale>
struct is_db_text_type<decimal<Precision, Scale>> : std::true_type {};

template <typename T>
inline constexpr bool is_db_text_type_v =
    is_db_text_type<std::remove_cv_t<T>>::value;

template <class T>
struct identity {};

// type_to_name mappings are always available (pure C++, no native DB headers).
// type_to_id (MySQL enum constants) requires <mysql.h> and is guarded below.

#ifdef ORMPP_ENABLE_MYSQL
#include <mysql.h>
#endif

namespace ormpp_mysql {
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
  return "BIGINT UNSIGNED"sv;
}
inline constexpr auto type_to_name(identity<blob>) noexcept { return "BLOB"sv; }
inline constexpr auto type_to_name(identity<date>) noexcept { return "DATE"sv; }
inline constexpr auto type_to_name(identity<time>) noexcept { return "TIME"sv; }
inline constexpr auto type_to_name(identity<datetime>) noexcept {
  return "DATETIME"sv;
}
inline constexpr auto type_to_name(identity<timestamp>) noexcept {
  return "TIMESTAMP"sv;
}
template <int Precision, int Scale>
inline auto type_to_name(identity<decimal<Precision, Scale>>) {
  return "DECIMAL(" + std::to_string(Precision) + "," + std::to_string(Scale) +
         ")";
}
inline auto type_to_name(identity<std::string>) noexcept { return "TEXT"sv; }
inline auto type_to_name(identity<std::string_view>) noexcept {
  return "TEXT"sv;
}
template <size_t N>
inline auto type_to_name(identity<std::array<char, N>>) noexcept {
  std::string s = "varchar(" + std::to_string(N) + ")";
  return s;
}

// type_to_id uses MYSQL_TYPE_* constants — only available with <mysql.h>
#ifdef ORMPP_ENABLE_MYSQL
inline int type_to_id(identity<char>) noexcept { return MYSQL_TYPE_TINY; }
inline int type_to_id(identity<short>) noexcept { return MYSQL_TYPE_SHORT; }
inline int type_to_id(identity<int>) noexcept { return MYSQL_TYPE_LONG; }
inline int type_to_id(identity<float>) noexcept { return MYSQL_TYPE_FLOAT; }
inline int type_to_id(identity<double>) noexcept { return MYSQL_TYPE_DOUBLE; }
inline int type_to_id(identity<int8_t>) noexcept { return MYSQL_TYPE_TINY; }
inline int type_to_id(identity<int64_t>) noexcept {
  return MYSQL_TYPE_LONGLONG;
}
inline int type_to_id(identity<uint8_t>) noexcept { return MYSQL_TYPE_TINY; }
inline int type_to_id(identity<uint16_t>) noexcept { return MYSQL_TYPE_SHORT; }
inline int type_to_id(identity<uint32_t>) noexcept { return MYSQL_TYPE_LONG; }
inline int type_to_id(identity<uint64_t>) noexcept {
  return MYSQL_TYPE_LONGLONG;
}
inline int type_to_id(identity<std::string>) noexcept {
  return MYSQL_TYPE_VAR_STRING;
}
inline int type_to_id(identity<std::string_view>) noexcept {
  return MYSQL_TYPE_VAR_STRING;
}
#endif  // ORMPP_ENABLE_MYSQL
}  // namespace ormpp_mysql

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
inline constexpr auto type_to_name(identity<date>) noexcept { return "TEXT"sv; }
inline constexpr auto type_to_name(identity<time>) noexcept { return "TEXT"sv; }
inline constexpr auto type_to_name(identity<datetime>) noexcept {
  return "TEXT"sv;
}
inline constexpr auto type_to_name(identity<timestamp>) noexcept {
  return "TEXT"sv;
}
template <int Precision, int Scale>
inline auto type_to_name(identity<decimal<Precision, Scale>>) {
  return "DECIMAL(" + std::to_string(Precision) + "," + std::to_string(Scale) +
         ")";
}
inline auto type_to_name(identity<std::string>) noexcept { return "TEXT"sv; }
inline auto type_to_name(identity<std::string_view>) noexcept {
  return "TEXT"sv;
}
template <size_t N>
inline auto type_to_name(identity<std::array<char, N>>) noexcept {
  std::string s = "varchar(" + std::to_string(N) + ")";
  return s;
}
}  // namespace ormpp_sqlite

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
inline constexpr auto type_to_name(identity<date>) noexcept { return "date"sv; }
inline constexpr auto type_to_name(identity<time>) noexcept { return "time"sv; }
inline constexpr auto type_to_name(identity<datetime>) noexcept {
  return "timestamp"sv;
}
inline constexpr auto type_to_name(identity<timestamp>) noexcept {
  return "timestamp"sv;
}
template <int Precision, int Scale>
inline auto type_to_name(identity<decimal<Precision, Scale>>) {
  return "numeric(" + std::to_string(Precision) + "," + std::to_string(Scale) +
         ")";
}
inline auto type_to_name(identity<std::string>) noexcept { return "text"sv; }
inline auto type_to_name(identity<std::string_view>) noexcept {
  return "text"sv;
}
template <size_t N>
inline auto type_to_name(identity<std::array<char, N>>) noexcept {
  std::string s = "varchar(" + std::to_string(N) + ")";
  return s;
}
}  // namespace ormpp_postgresql

}  // namespace ormpp

#endif  // EXAMPLE1_TYPE_MAPPING_HPP
