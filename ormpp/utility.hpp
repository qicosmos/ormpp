//
// Created by qiyu on 10/29/17.
//
#ifndef ORM_UTILITY_HPP
#define ORM_UTILITY_HPP
#include <algorithm>
#include <iostream>
#include <optional>
#include <sstream>

#include "entity.hpp"
#include "iguana/util.hpp"
#include "type_mapping.hpp"

namespace ormpp {

//-------------------------------------------------------------------------------------------------------------//
//-------------------------------------------------------------------------------------------------------------//
template <auto... members>
constexpr std::array<size_t, sizeof...(members)> indexs_of() {
  return std::array<size_t, sizeof...(members)>{
      ylt::reflection::index_of<members>()...};
}

template <typename... Args, typename F, std::size_t... Idx>
constexpr void for_each(std::tuple<Args...> &t, F &&f,
                        std::index_sequence<Idx...>) {
  (std::forward<F>(f)(std::get<Idx>(t), std::integral_constant<size_t, Idx>{}),
   ...);
}

template <typename... Args, typename F, std::size_t... Idx>
constexpr void for_each(const std::tuple<Args...> &t, F &&f,
                        std::index_sequence<Idx...>) {
  (std::forward<F>(f)(std::get<Idx>(t), std::integral_constant<size_t, Idx>{}),
   ...);
}
//-------------------------------------------------------------------------------------------------------------//
//-------------------------------------------------------------------------------------------------------------//

inline auto &get_auto_key_map() {
  static std::unordered_map<std::string_view, std::string_view> map;
  return map;
}

inline int add_auto_key_field(std::string_view key, std::string_view value) {
  get_auto_key_map().emplace(key, value);
  return 0;
}

template <typename T>
inline auto get_auto_key() {
  auto it = get_auto_key_map().find(ylt::reflection::type_string<T>());
  return it == get_auto_key_map().end() ? "" : it->second;
}

template <typename T>
inline auto is_auto_key(std::string_view field_name) {
  auto it = get_auto_key_map().find(ylt::reflection::type_string<T>());
  return it == get_auto_key_map().end() ? false : it->second == field_name;
}

#ifdef _MSC_VER
#define ORMPP_UNIQUE_VARIABLE(str) YLT_CONCAT(str, __COUNTER__)
#else
#define ORMPP_UNIQUE_VARIABLE(str) YLT_CONCAT(str, __LINE__)
#endif

#define REGISTER_AUTO_KEY(STRUCT_NAME, KEY)        \
  inline auto ORMPP_UNIQUE_VARIABLE(STRUCT_NAME) = \
      ormpp::add_auto_key_field(#STRUCT_NAME, #KEY);

inline auto &get_conflict_map() {
  static std::unordered_map<std::string_view, std::string_view> map;
  return map;
}

inline int add_conflict_key_field(std::string_view key,
                                  std::string_view value) {
  get_conflict_map().emplace(key, value);
  return 0;
}

template <typename T>
inline auto get_conflict_key() {
  auto it = get_conflict_map().find(ylt::reflection::type_string<T>());
  if (it == get_conflict_map().end()) {
    auto auto_key = get_auto_key_map().find(ylt::reflection::type_string<T>());
    return auto_key == get_auto_key_map().end() ? "" : auto_key->second;
  }
  return it->second;
}

#define MAKE_NAMES(...) #__VA_ARGS__,

#define REGISTER_CONFLICT_KEY(STRUCT_NAME, ...)    \
  inline auto ORMPP_UNIQUE_VARIABLE(STRUCT_NAME) = \
      ormpp::add_conflict_key_field(#STRUCT_NAME, {MAKE_NAMES(__VA_ARGS__)});

template <typename T>
struct is_optional_v : std::false_type {};

template <typename T>
struct is_optional_v<std::optional<T>> : std::true_type {};

template <typename T>
constexpr std::enable_if_t<iguana::ylt_refletable_v<T>, size_t> get_value() {
  return ylt::reflection::members_count_v<T>;
}

template <typename T>
constexpr std::enable_if_t<iguana::non_ylt_refletable_v<T>, size_t>
get_value() {
  return 1;
}

template <typename... Args>
struct value_of;

template <typename T>
struct value_of<T> {
  static const auto value = (get_value<T>());
};

template <typename T, typename... Rest>
struct value_of<T, Rest...> {
  static const auto value = (value_of<T>::value + value_of<Rest...>::value);
};

template <typename List>
struct result_size;

template <template <class...> class List, class... T>
struct result_size<List<T...>> {
  constexpr static const size_t value =
      value_of<T...>::value;  // (iguana::get_value<T>() + ...);
};

template <typename T>
inline void append_impl(std::string &sql, const T &str) {
  if constexpr (std::is_same_v<std::string, T> ||
                std::is_same_v<std::string_view, T>) {
    if (str.empty())
      return;
  }
  else {
    if constexpr (sizeof(str) == 0) {
      return;
    }
  }

  sql += str;
  sql += " ";
}

template <typename... Args>
inline void append(std::string &sql, Args &&...args) {
  (append_impl(sql, std::forward<Args>(args)), ...);
}

template <typename... Args>
inline auto sort_tuple(const std::tuple<Args...> &tp) {
  if constexpr (sizeof...(Args) == 2) {
    auto [a, b] = tp;
    if constexpr (!std::is_same_v<decltype(a), ormpp_key> &&
                  !std::is_same_v<decltype(a), ormpp_auto_key>)
      return std::make_tuple(b, a);
    else
      return tp;
  }
  else {
    return tp;
  }
}

enum class OptType { insert, update, replace };
enum class DBType { mysql, sqlite, postgresql, unknown };

template <typename T>
inline constexpr auto get_type_names(DBType type) {
  std::array<std::string, ylt::reflection::members_count_v<T>> arr = {};
  ylt::reflection::for_each(T{}, [&arr, type](auto &field, auto /*name*/,
                                              auto index) {
    using U = ylt::reflection::remove_cvref_t<decltype(field)>;
    std::string s;
    if (type == DBType::unknown) {
    }
#ifdef ORMPP_ENABLE_MYSQL
    else if (type == DBType::mysql) {
      if constexpr (std::is_enum_v<U>) {
        s = "INTEGER"sv;
      }
      else if constexpr (is_optional_v<U>::value) {
        s = ormpp_mysql::type_to_name(identity<typename U::value_type>{});
      }
#ifdef ORMPP_WITH_CSTRING
      else if constexpr (std::is_same_v<CString, U>) {
        s = "TEXT"sv;
      }
#endif
      else {
        s = ormpp_mysql::type_to_name(identity<U>{});
      }
    }
#endif
#ifdef ORMPP_ENABLE_SQLITE3
    else if (type == DBType::sqlite) {
      if constexpr (std::is_enum_v<U>) {
        s = "INTEGER"sv;
      }
      else if constexpr (is_optional_v<U>::value) {
        s = ormpp_sqlite::type_to_name(identity<typename U::value_type>{});
      }
#ifdef ORMPP_WITH_CSTRING
      else if constexpr (std::is_same_v<CString, U>) {
        s = "TEXT"sv;
      }
#endif
      else {
        s = ormpp_sqlite::type_to_name(identity<U>{});
      }
    }
#endif
#ifdef ORMPP_ENABLE_PG
    else if (type == DBType::postgresql) {
      if constexpr (std::is_enum_v<U>) {
        s = "integer"sv;
      }
      else if constexpr (is_optional_v<U>::value) {
        s = ormpp_postgresql::type_to_name(identity<typename U::value_type>{});
      }
#ifdef ORMPP_WITH_CSTRING
      else if constexpr (std::is_same_v<CString, U>) {
        s = "TEXT"sv;
      }
#endif
      else {
        s = ormpp_postgresql::type_to_name(identity<U>{});
      }
    }
#endif

    arr[index] = s;
  });

  return arr;
}

template <typename... Args, typename Func, std::size_t... Idx>
inline void for_each0(const std::tuple<Args...> &t, Func &&f,
                      std::index_sequence<Idx...>) {
  (f(std::get<Idx>(t)), ...);
}

template <typename T, typename = std::enable_if_t<iguana::ylt_refletable_v<T>>>
inline std::string get_struct_name() {
#ifdef ORMPP_ENABLE_PG
  std::string quota_name =
      "\"" + std::string(ylt::reflection::get_struct_name<T>()) + "\"";
#else
  std::string quota_name =
      "`" + std::string(ylt::reflection::get_struct_name<T>()) + "`";
#endif
  return quota_name;
}

template <typename T, typename = std::enable_if_t<iguana::ylt_refletable_v<T>>>
inline std::string get_fields() {
  static std::string fields;
  if (!fields.empty()) {
    return fields;
  }
  for (const auto &it : ylt::reflection::get_member_names<T>()) {
#ifdef ORMPP_ENABLE_MYSQL
    fields += "`" + std::string(it) + "`";
#else
    fields += it;
#endif
    fields += ",";
  }
  fields.back() = ' ';
  return fields;
}

template <typename T, typename = std::enable_if_t<iguana::ylt_refletable_v<T>>>
inline std::vector<std::string> get_conflict_keys() {
  static std::vector<std::string> res;
  if (!res.empty()) {
    return res;
  }
  std::stringstream s;
  s << get_conflict_key<T>();
  while (s.good()) {
    std::string str;
    getline(s, str, ',');
    if (str.front() == ' ') {
      str.erase(0);
    }
    if (str.back() == ' ') {
      str.pop_back();
    }
#ifdef ORMPP_ENABLE_MYSQL
    str.insert(0, "`");
    str.append("`");
#endif
    res.emplace_back(str);
  }
  return res;
}

template <typename T>
inline auto is_conflict_key(std::string_view field_name) {
  for (const auto &it : get_conflict_keys<T>()) {
    if (it == field_name) {
      return true;
    }
  }
  return false;
}

template <typename T, typename... Args>
inline std::string generate_insert_sql(bool insert, Args &&...args) {
#ifdef ORMPP_ENABLE_PG
  if (!insert) {
    constexpr auto Count = ylt::reflection::members_count_v<T>;
    std::string sql = "insert into ";
    auto name = get_struct_name<T>();
    append(sql, name);
    int index = 0;
    std::string set;
    std::string fields = "(";
    std::string values = "values(";
    for (auto i = 0; i < Count; ++i) {
      std::string field_name(ylt::reflection::name_of<T>(i));
      std::string value = "$" + std::to_string(++index);
      append(set, field_name, "=", value);
      fields += field_name;
      values += value;
      if (i < Count - 1) {
        fields += ",";
        values += ",";
        set += ",";
      }
      else {
        fields += ")";
        values += ")";
        set += ";";
      }
    }
    std::string conflict = "on conflict(";
    if constexpr (sizeof...(Args) > 0) {
      append(conflict, args...);
    }
    else {
      conflict += get_conflict_key<T>();
    }
    conflict += ")";
    append(sql, fields, values, conflict, "do update set", set);
    return sql;
  }
#endif
  std::string sql = insert ? "insert into " : "replace into ";
  constexpr auto Count = ylt::reflection::members_count_v<T>;
  auto name = get_struct_name<T>();
  append(sql, name);

  int index = 0;
  std::string fields = "(";
  std::string values = "values(";
  for (size_t i = 0; i < Count; ++i) {
    std::string field_name(ylt::reflection::name_of<T>(i));
    if (insert && is_auto_key<T>(field_name)) {
      continue;
    }
#ifdef ORMPP_ENABLE_PG
    values += "$" + std::to_string(++index);
#else
    values += "?";
#endif
#ifdef ORMPP_ENABLE_MYSQL
    fields += "`" + field_name + "`";
#else
    fields += field_name;
#endif
    if (i < Count - 1) {
      fields += ",";
      values += ",";
    }
    else {
      fields += ")";
      values += ")";
    }
  }
  if (fields.back() != ')') {
    fields.back() = ')';
  }
  if (values.back() != ')') {
    values.back() = ')';
  }
  append(sql, fields, values);
  return sql;
}

template <typename T, auto... members, typename... Args>
inline std::string generate_update_sql(Args &&...args) {
  std::string sql, fields;
  append(sql, "update", get_struct_name<T>(), "set");

#ifdef ORMPP_ENABLE_PG
  size_t index = 0;
#endif

  if constexpr (sizeof...(members) > 0) {
#ifdef ORMPP_ENABLE_PG
    (fields
         .append(
             ylt::reflection::name_of<T>(ylt::reflection::index_of<members>()))
         .append("=$" + std::to_string(++index) + ","),
     ...);
#else
    (fields
         .append(
             ylt::reflection::name_of<T>(ylt::reflection::index_of<members>()))
         .append("=?,"),
     ...);
#endif
  }
  else {
    constexpr auto Count = ylt::reflection::members_count_v<T>;
    for (size_t i = 0; i < Count; ++i) {
      std::string field_name(ylt::reflection::name_of<T>(i));
#ifdef ORMPP_ENABLE_MYSQL
      fields.append("`").append(field_name).append("`");
#else
      fields.append(field_name);
#endif
#ifdef ORMPP_ENABLE_PG
      fields.append("=$").append(std::to_string(++index)).append(",");
#else
      fields.append("=?,");
#endif
    }
  }
  fields.pop_back();

  std::string conflict = "where 1=1";
  if constexpr (sizeof...(Args) > 0) {
    append(conflict, " and", args...);
  }
  else {
    for (const auto &it : get_conflict_keys<T>()) {
#ifdef ORMPP_ENABLE_PG
      append(conflict, " and", it + "=$" + std::to_string(++index));
#else
      append(conflict, " and", it + "=?");
#endif
    }
  }

  append(sql, fields, conflict);
  sql.pop_back();
  sql.pop_back();
  return sql;
}

inline bool is_empty(const std::string &t) { return t.empty(); }

template <typename T, typename... Args>
inline std::string generate_delete_sql(Args &&...where_conditon) {
  std::string sql = "delete from ";
  auto name = get_struct_name<T>();
  append(sql, name);
  if constexpr (sizeof...(Args) > 0) {
    if (!is_empty(std::forward<Args>(where_conditon)...))
      append(sql, "where", std::forward<Args>(where_conditon)...);
  }
  return sql;
}

inline void get_sql_conditions(std::string &) {}

template <typename... Args>
inline void get_sql_conditions(std::string &sql, const std::string &arg,
                               Args &&...args) {
  std::string temp = arg;
  std::transform(arg.begin(), arg.end(), temp.begin(), ::tolower);
  if (temp.find("select") != std::string::npos) {
    sql = arg;
  }
  else {
    if (temp.find("order by") != std::string::npos) {
      auto pos = sql.find("where");
      sql = sql.substr(0, pos);
    }
    if (temp.find("limit") != std::string::npos) {
      auto pos = sql.find("where");
      sql = sql.substr(0, pos);
    }
    append(sql, arg, std::forward<Args>(args)...);
  }
}

template <typename T, typename... Args>
inline std::string generate_query_sql(Args &&...args) {
  bool where = false;
  std::string sql = "select ";
  auto fields = get_fields<T>();
  auto name = get_struct_name<T>();
  append(sql, fields, "from", name);
  if constexpr (sizeof...(Args) > 0) {
    using expander = int[];
    expander{0, (where = where ? where : !is_empty(args), 0)...};
  }
  if (where) {
    append(sql, "where 1=1 and ");
  }
  get_sql_conditions(sql, std::forward<Args>(args)...);
  return sql;
}

template <typename T>
inline constexpr auto to_str(T &&t) {
  if constexpr (std::is_arithmetic_v<std::decay_t<T>>) {
    return std::to_string(std::forward<T>(t));
  }
  else {
    return std::string("'") + t + std::string("'");
  }
}

inline void get_str(std::string &sql, const std::string &s) {
  auto pos = sql.find_first_of('?');
  sql.replace(pos, 1, " ");
  sql.insert(pos, s);
}

template <typename... Args>
inline std::string get_sql(const std::string &o, Args &&...args) {
  auto sql = o;
  std::string s = "";
  (get_str(sql, to_str(args)), ...);

  return sql;
}

template <typename T>
struct field_attribute;

template <typename T, typename U>
struct field_attribute<U T::*> {
  using type = T;
  using return_type = U;
};

template <typename U>
constexpr std::string_view get_field_name(std::string_view full_name) {
  using T = typename field_attribute<U>::type;
  return ylt::reflection::get_member_names<T>()[ylt::reflection::index_of<T>(
      full_name.substr(full_name.rfind(":") + 1))];
}

#define FID(field)                                                       \
  std::pair<std::string_view, decltype(&field)>(                         \
      ormpp::get_field_name<decltype(&field)>(std::string_view(#field)), \
      &field)
#define SID(field) \
  get_field_name<decltype(&field)>(std::string_view(#field)).data()
}  // namespace ormpp

#endif  // ORM_UTILITY_HPP