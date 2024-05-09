//
// Created by qiyu on 10/30/17.
//

#ifndef ORM_POSTGRESQL_HPP
#define ORM_POSTGRESQL_HPP

#include <libpq-fe.h>

#include <climits>
#include <string>
#include <type_traits>

#include "iguana/detail/charconv.h"
#include "utility.hpp"

using namespace std::string_literals;

namespace ormpp {
class postgresql {
 public:
  ~postgresql() { disconnect(); }

  bool has_error() const { return has_error_; }

  static void reset_error() {
    has_error_ = false;
    last_error_ = {};
  }

  static void set_last_error(std::string last_error) {
    has_error_ = true;
    last_error_ = std::move(last_error);
    std::cout << last_error_ << std::endl;
  }

  std::string get_last_error() const { return last_error_; }

  // ip, user, pwd, db, timeout  the sequence must be fixed like this
  template <typename... Args>
  bool connect(Args &&...args) {
    reset_error();
    auto sql = generate_conn_sql(std::make_tuple(std::forward<Args>(args)...));
#ifdef ORMPP_ENABLE_LOG
    std::cout << sql << std::endl;
#endif
    con_ = PQconnectdb(sql.data());
    if (PQstatus(con_) != CONNECTION_OK) {
      set_last_error(PQerrorMessage(con_));
      return false;
    }
    return true;
  }

  template <typename... Args>
  bool disconnect(Args &&...args) {
    if (con_ != nullptr) {
      PQfinish(con_);
      con_ = nullptr;
    }
    return true;
  }

  bool ping() { return (PQstatus(con_) == CONNECTION_OK); }

  template <typename T, typename... Args>
  bool create_datatable(Args &&...args) {
    std::string sql = generate_createtb_sql<T>(std::forward<Args>(args)...);
#ifdef ORMPP_ENABLE_LOG
    std::cout << sql << std::endl;
#endif
    res_ = PQexec(con_, sql.data());
    auto guard = guard_statment(res_);
    return PQresultStatus(res_) == PGRES_COMMAND_OK;
  }

  template <typename T, typename... Args>
  int insert(const T &t, Args &&...args) {
    return insert_impl(OptType::insert, t, std::forward<Args>(args)...);
  }

  template <typename T, typename... Args>
  int insert(const std::vector<T> &v, Args &&...args) {
    return insert_impl(OptType::insert, v, std::forward<Args>(args)...);
  }

  template <typename T, typename... Args>
  int replace(const T &t, Args &&...args) {
    return insert_impl(OptType::replace, t, std::forward<Args>(args)...);
  }

  template <typename T, typename... Args>
  int replace(const std::vector<T> &v, Args &&...args) {
    return insert_impl(OptType::replace, v, std::forward<Args>(args)...);
  }

  template <auto... members, typename T, typename... Args>
  int update(const T &t, Args &&...args) {
    return update_impl<members...>(t, std::forward<Args>(args)...);
  }

  template <auto... members, typename T, typename... Args>
  int update(const std::vector<T> &v, Args &&...args) {
    return update_impl<members...>(v, std::forward<Args>(args)...);
  }

  template <typename T, typename... Args>
  uint64_t get_insert_id_after_insert(const T &t, Args &&...args) {
    auto res = insert_or_update_impl(t, generate_insert_sql<T>(true),
                                     OptType::insert, true);
    return res.has_value() ? res.value() : 0;
  }

  template <typename T, typename... Args>
  uint64_t get_insert_id_after_insert(const std::vector<T> &v, Args &&...args) {
    auto res = insert_or_update_impl(v, generate_insert_sql<T>(true),
                                     OptType::insert, true);
    return res.has_value() ? res.value() : 0;
  }

  template <typename T, typename... Args>
  constexpr bool delete_records(Args &&...where_conditon) {
    auto sql = generate_delete_sql<T>(std::forward<Args>(where_conditon)...);
    return execute(sql);
  }

  template <typename T, typename... Args>
  bool delete_records_s(const std::string &str, Args &&...args) {
    auto sql = generate_delete_sql<T>(str);
#ifdef ORMPP_ENABLE_LOG
    std::cout << sql << std::endl;
#endif
    if (!prepare<T>(sql))
      return false;

    if constexpr (sizeof...(Args) > 0) {
      size_t index = 0;
      std::vector<const char *> param_values_buf;
      std::vector<std::vector<char>> param_values;
      (set_param_values(param_values, args), ...);
      for (auto &item : param_values) {
        param_values_buf.push_back(item.data());
      }
      res_ = PQexecPrepared(con_, "", (int)param_values.size(),
                            param_values_buf.data(), NULL, NULL, 0);
    }
    else {
      res_ = PQexec(con_, sql.data());
    }

    auto guard = guard_statment(res_);
    if (PQresultStatus(res_) != PGRES_COMMAND_OK) {
      return false;
    }
    return true;
  }

  template <typename T, typename... Args>
  std::enable_if_t<iguana::is_reflection_v<T>, std::vector<T>> query_s(
      const std::string &str, Args &&...args) {
    std::string sql = generate_query_sql<T>(str);
#ifdef ORMPP_ENABLE_LOG
    std::cout << sql << std::endl;
#endif
    if (!prepare<T>(sql))
      return {};

    if constexpr (sizeof...(Args) > 0) {
      size_t index = 0;
      std::vector<const char *> param_values_buf;
      std::vector<std::vector<char>> param_values;
      (set_param_values(param_values, args), ...);
      for (auto &item : param_values) {
        param_values_buf.push_back(item.data());
      }
      res_ = PQexecPrepared(con_, "", (int)param_values.size(),
                            param_values_buf.data(), NULL, NULL, 0);
    }
    else {
      res_ = PQexec(con_, sql.data());
    }

    auto guard = guard_statment(res_);
    if (PQresultStatus(res_) != PGRES_TUPLES_OK) {
      return {};
    }

    std::vector<T> v;
    auto ntuples = PQntuples(res_);

    for (auto i = 0; i < ntuples; i++) {
      T t = {};
      iguana::for_each(t, [this, i, &t](auto item, auto I) {
        assign(t.*item, i, (int)decltype(I)::value);
      });
      v.push_back(std::move(t));
    }

    return v;
  }

  template <typename T, typename... Args>
  std::enable_if_t<!iguana::is_reflection_v<T>, std::vector<T>> query_s(
      const std::string &sql, Args &&...args) {
    static_assert(iguana::is_tuple<T>::value);
#ifdef ORMPP_ENABLE_LOG
    std::cout << sql << std::endl;
#endif
    if (!prepare<T>(sql))
      return {};

    if constexpr (sizeof...(Args) > 0) {
      size_t index = 0;
      std::vector<const char *> param_values_buf;
      std::vector<std::vector<char>> param_values;
      (set_param_values(param_values, args), ...);
      for (auto &item : param_values) {
        param_values_buf.push_back(item.data());
      }
      res_ = PQexecPrepared(con_, "", (int)param_values.size(),
                            param_values_buf.data(), NULL, NULL, 0);
    }
    else {
      res_ = PQexec(con_, sql.data());
    }

    auto guard = guard_statment(res_);
    if (PQresultStatus(res_) != PGRES_TUPLES_OK) {
      return {};
    }

    std::vector<T> v;
    auto ntuples = PQntuples(res_);

    for (auto i = 0; i < ntuples; i++) {
      T tp = {};
      int index = 0;
      iguana::for_each(
          tp,
          [this, i, &index](auto &item, auto I) {
            if constexpr (iguana::is_reflection_v<decltype(item)>) {
              std::remove_reference_t<decltype(item)> t = {};
              iguana::for_each(t, [this, &index, &t, i](auto ele, auto /*i*/) {
                assign(t.*ele, (int)i, index++);
              });
              item = std::move(t);
            }
            else {
              assign(item, (int)i, index++);
            }
          },
          std::make_index_sequence<std::tuple_size_v<T>>{});

      if (index > 0)
        v.push_back(std::move(tp));
    }

    return v;
  }

  template <typename T, typename... Args>
  std::enable_if_t<iguana::is_reflection_v<T>, std::vector<T>> query(
      Args &&...args) {
    std::string sql = generate_query_sql<T>(args...);
#ifdef ORMPP_ENABLE_LOG
    std::cout << sql << std::endl;
#endif
    if (!prepare<T>(sql))
      return {};

    res_ = PQexec(con_, sql.data());
    auto guard = guard_statment(res_);
    if (PQresultStatus(res_) != PGRES_TUPLES_OK) {
      return {};
    }

    std::vector<T> v;
    auto ntuples = PQntuples(res_);

    for (auto i = 0; i < ntuples; i++) {
      T t = {};
      iguana::for_each(t, [this, i, &t](auto item, auto I) {
        assign(t.*item, i, (int)decltype(I)::value);
      });
      v.push_back(std::move(t));
    }

    return v;
  }

  template <typename T, typename Arg, typename... Args>
  std::enable_if_t<!iguana::is_reflection_v<T>, std::vector<T>> query(
      const Arg &s, Args &&...args) {
    static_assert(iguana::is_tuple<T>::value);
    constexpr auto SIZE = std::tuple_size_v<T>;

    std::string sql = s;
    constexpr auto Args_Size = sizeof...(Args);
    if (Args_Size != 0) {
      if (Args_Size != std::count(sql.begin(), sql.end(), '$'))
        return {};

      sql = get_sql(sql, std::forward<Args>(args)...);
    }

    if (!prepare<T>(sql))
      return {};

    res_ = PQexec(con_, sql.data());
    auto guard = guard_statment(res_);
    if (PQresultStatus(res_) != PGRES_TUPLES_OK) {
      return {};
    }

    std::vector<T> v;
    auto ntuples = PQntuples(res_);

    for (auto i = 0; i < ntuples; i++) {
      T tp = {};
      int index = 0;
      iguana::for_each(
          tp,
          [this, i, &index](auto &item, auto I) {
            if constexpr (iguana::is_reflection_v<decltype(item)>) {
              std::remove_reference_t<decltype(item)> t = {};
              iguana::for_each(t, [this, &index, &t, i](auto ele, auto /*i*/) {
                assign(t.*ele, (int)i, index++);
              });
              item = std::move(t);
            }
            else {
              assign(item, (int)i, index++);
            }
          },
          std::make_index_sequence<SIZE>{});

      if (index > 0)
        v.push_back(std::move(tp));
    }

    return v;
  }

  // just support execute string sql without placeholders
  bool execute(const std::string &sql) {
#ifdef ORMPP_ENABLE_LOG
    std::cout << sql << std::endl;
#endif
    res_ = PQexec(con_, sql.data());
    auto guard = guard_statment(res_);
    return PQresultStatus(res_) == PGRES_COMMAND_OK;
  }

  // transaction
  bool begin() {
    res_ = PQexec(con_, "begin;");
    auto guard = guard_statment(res_);
    return PQresultStatus(res_) == PGRES_COMMAND_OK;
  }

  bool commit() {
    res_ = PQexec(con_, "commit;");
    auto guard = guard_statment(res_);
    return PQresultStatus(res_) == PGRES_COMMAND_OK;
  }

  bool rollback() {
    res_ = PQexec(con_, "rollback;");
    auto guard = guard_statment(res_);
    return PQresultStatus(res_) == PGRES_COMMAND_OK;
  }

 private:
  template <typename T>
  auto to_str(T &&t) {
    if constexpr (std::is_integral_v<std::decay_t<T>>)
      return std::to_string(std::forward<T>(t));
    else
      return t;
  }

  template <typename Tuple>
  std::string generate_conn_sql(const Tuple &tp) {
    constexpr size_t SIZE = std::tuple_size_v<Tuple>;
    if constexpr (SIZE == 4) {
      return generate_conn_sql(
          std::make_tuple("host", "user", "password", "dbname"), tp,
          std::make_index_sequence<SIZE>{});
    }
    else if constexpr (SIZE == 5) {
      return generate_conn_sql(std::make_tuple("host", "user", "password",
                                               "dbname", "connect_timeout"),
                               tp, std::make_index_sequence<SIZE>{});
    }
    else if constexpr (SIZE == 6) {
      return generate_conn_sql(
          std::make_tuple("host", "user", "password", "dbname",
                          "connect_timeout", "port"),
          tp, std::make_index_sequence<SIZE>{});
    }
    else {
      return "";
    }
  }

  template <typename... Args1, typename... Args2, std::size_t... Idx>
  std::string generate_conn_sql(const std::tuple<Args1...> &tp1,
                                const std::tuple<Args2...> &tp2,
                                std::index_sequence<Idx...>) {
    std::string sql = "";
    (append(sql, std::get<Idx>(tp1), "=", to_str(std::get<Idx>(tp2)), " "),
     ...);
    return sql;
  }

  template <typename T, typename... Args>
  std::string generate_createtb_sql(Args &&...args) {
    const auto type_name_arr = get_type_names<T>(DBType::postgresql);
    auto name = get_name<T>();
    std::string sql =
        std::string("CREATE TABLE IF NOT EXISTS ") + name.data() + "(";
    auto arr = iguana::get_array<T>();
    constexpr const size_t SIZE = sizeof...(Args);

    // auto_increment_key and key can't exist at the same time
    using U = std::tuple<std::decay_t<Args>...>;
    if constexpr (SIZE > 0) {
      // using U = std::tuple<std::decay_t <Args>...>; //the code can't compile
      // in vs2017, why?maybe args... in if constexpr?
      static_assert(!(iguana::has_type<ormpp_key, U>::value &&
                      iguana::has_type<ormpp_auto_key, U>::value),
                    "should only one key");
    }

    // at first sort the args, make sure the key always in the head
    auto tp = sort_tuple(std::make_tuple(std::forward<Args>(args)...));
    const size_t arr_size = arr.size();
    std::set<std::string> unique_fields;
    for (size_t i = 0; i < arr_size; ++i) {
      auto field_name = arr[i];
      bool has_add_field = false;
      iguana::for_each(
          tp,
          [&sql, &i, &has_add_field, &unique_fields, field_name, type_name_arr,
           name, this](auto item, auto I) {
            if constexpr (std::is_same_v<decltype(item), ormpp_not_null> ||
                          std::is_same_v<decltype(item), ormpp_unique>) {
              if (item.fields.find(field_name.data()) == item.fields.end())
                return;
            }
            else {
              if (item.fields != field_name.data())
                return;
            }

            if constexpr (std::is_same_v<decltype(item), ormpp_not_null>) {
              if (!has_add_field) {
                append(sql, field_name.data(), " ", type_name_arr[i]);
              }
              append(sql, " NOT NULL");
              has_add_field = true;
            }
            else if constexpr (std::is_same_v<decltype(item), ormpp_key>) {
              if (!has_add_field) {
                append(sql, field_name.data(), " ", type_name_arr[i]);
              }
              append(sql, " PRIMARY KEY ");
              has_add_field = true;
            }
            else if constexpr (std::is_same_v<decltype(item), ormpp_auto_key>) {
              if (!has_add_field) {
                if (type_name_arr[i] == "bigint") {
                  append(sql, field_name.data(), " ", "bigserial");
                }
                else {
                  append(sql, field_name.data(), " ", "serial");
                }
              }
              append(sql, " primary key");
              has_add_field = true;
            }
            else if constexpr (std::is_same_v<decltype(item), ormpp_unique>) {
              unique_fields.insert(field_name.data());
            }
            else {
              append(sql, field_name.data(), " ", type_name_arr[i]);
            }
          },
          std::make_index_sequence<SIZE>{});

      if (!has_add_field) {
        append(sql, field_name.data(), " ", type_name_arr[i]);
      }

      if (i < arr_size - 1)
        sql += ", ";
    }

    if (!unique_fields.empty()) {
      sql += ", UNIQUE(";
      for (const auto &it : unique_fields) {
        sql += it + ",";
      }
      sql.back() = ')';
    }

    sql += ")";

    return sql;
  }

  template <typename T>
  bool prepare(const std::string &sql) {
#ifdef ORMPP_ENABLE_LOG
    std::cout << sql << std::endl;
#endif
    res_ =
        PQprepare(con_, "", sql.data(), (int)iguana::get_value<T>(), nullptr);
    auto guard = guard_statment(res_);
    return PQresultStatus(res_) == PGRES_COMMAND_OK;
  }

  template <auto... members, typename T, typename... Args>
  std::optional<uint64_t> stmt_execute(const T &t, OptType type,
                                       Args &&...args) {
    std::vector<std::vector<char>> param_values;
    constexpr auto arr = iguana::indexs_of<members...>();
    iguana::for_each(t,
                     [&t, arr, &param_values, type, this](auto item, auto i) {
                       if (type == OptType::insert &&
                           is_auto_key<T>(iguana::get_name<T>(i).data())) {
                         return;
                       }
                       if constexpr (sizeof...(members) > 0) {
                         for (auto idx : arr) {
                           if (idx == decltype(i)::value) {
                             set_param_values(param_values, t.*item);
                           }
                         }
                       }
                       else {
                         set_param_values(param_values, t.*item);
                       }
                     });

    if constexpr (sizeof...(Args) == 0) {
      if (type == OptType::update) {
        iguana::for_each(t, [&t, &param_values, this](auto item, auto i) {
          if (is_conflict_key<T>(iguana::get_name<T>(i).data())) {
            set_param_values(param_values, t.*item);
          }
        });
      }
    }

    if (param_values.empty()) {
      return std::nullopt;
    }

    std::vector<const char *> param_values_buf;
    for (auto &item : param_values) {
      param_values_buf.push_back(item.data());
    }

    res_ = PQexecPrepared(con_, "", (int)param_values.size(),
                          param_values_buf.data(), NULL, NULL, 0);

    auto guard = guard_statment(res_);
    auto status = PQresultStatus(res_);

    if (status == PGRES_TUPLES_OK) {
      return std::strtoull(PQgetvalue(res_, 0, 0), nullptr, 10);
    }
    else if (status == PGRES_COMMAND_OK) {
      return 1;
    }

    return std::nullopt;
  }

  template <typename T, typename... Args>
  int insert_impl(OptType type, const T &t, Args &&...args) {
    auto res = insert_or_update_impl(
        t,
        generate_insert_sql<T>(type == OptType::insert,
                               std::forward<Args>(args)...),
        type);
    return res.has_value() ? res.value() : INT_MIN;
  }

  template <typename T, typename... Args>
  int insert_impl(OptType type, const std::vector<T> &v, Args &&...args) {
    auto res = insert_or_update_impl(
        v,
        generate_insert_sql<T>(type == OptType::insert,
                               std::forward<Args>(args)...),
        type);
    return res.has_value() ? res.value() : INT_MIN;
  }

  template <auto... members, typename T, typename... Args>
  int update_impl(const T &t, Args &&...args) {
    auto sql = generate_update_sql<T, members...>(std::forward<Args>(args)...);
    auto res = insert_or_update_impl<members...>(t, sql, OptType::update, false,
                                                 std::forward<Args>(args)...);
    return res.has_value() ? res.value() : INT_MIN;
  }

  template <auto... members, typename T, typename... Args>
  int update_impl(const std::vector<T> &v, Args &&...args) {
    auto sql = generate_update_sql<T, members...>(std::forward<Args>(args)...);
    auto res = insert_or_update_impl<members...>(v, sql, OptType::update, false,
                                                 std::forward<Args>(args)...);
    return res.has_value() ? res.value() : INT_MIN;
  }

  template <auto... members, typename T, typename... Args>
  std::optional<uint64_t> insert_or_update_impl(const T &t,
                                                const std::string &sql,
                                                OptType type,
                                                bool get_insert_id = false,
                                                Args &&...args) {
    if (!prepare<T>(get_insert_id
                        ? sql + "returning " + get_auto_key<T>().data()
                        : sql)) {
      return std::nullopt;
    }

    return stmt_execute<members...>(t, type, std::forward<Args>(args)...);
  }

  template <auto... members, typename T, typename... Args>
  std::optional<uint64_t> insert_or_update_impl(const std::vector<T> &v,
                                                const std::string &sql,
                                                OptType type,
                                                bool get_insert_id = false,
                                                Args &&...args) {
    if (!begin()) {
      return std::nullopt;
    }

    if (!prepare<T>(get_insert_id
                        ? sql + "returning " + get_auto_key<T>().data()
                        : sql)) {
      return std::nullopt;
    }

    std::optional<uint64_t> res = {0};
    for (auto &item : v) {
      res = stmt_execute<members...>(item, type, std::forward<Args>(args)...);
      if (!res.has_value()) {
        rollback();
        return std::nullopt;
      }
    }

    if (!commit()) {
      return std::nullopt;
    }

    return get_insert_id ? res : (int)v.size();
  }

  template <typename T>
  void set_param_values(std::vector<std::vector<char>> &param_values,
                        T &&value) {
    using U = std::remove_const_t<std::remove_reference_t<T>>;
    if constexpr (is_optional_v<U>::value) {
      if (value.has_value()) {
        return set_param_values(param_values, std::move(value.value()));
      }
      else {
        param_values.push_back({});
      }
    }
    else if constexpr (std::is_enum_v<U> && !iguana::is_int64_v<U>) {
      std::vector<char> temp(20, 0);
      iguana::detail::to_chars(temp.data(), static_cast<int>(value));
      param_values.push_back(std::move(temp));
    }
    else if constexpr (std::is_integral_v<U> && !iguana::is_int64_v<U>) {
      std::vector<char> temp(20, 0);
      if constexpr (std::is_same_v<char, U>) {
        temp.front() = value;
      }
      else if constexpr (iguana::is_char_type<U>::value) {
        itoa_fwd(value, temp.data());
      }
      else {
        iguana::detail::to_chars(temp.data(), value);
      }
      param_values.push_back(std::move(temp));
    }
    else if constexpr (iguana::is_int64_v<U>) {
      std::vector<char> temp(65, 0);
      iguana::detail::to_chars(temp.data(), value);
      param_values.push_back(std::move(temp));
    }
    else if constexpr (std::is_floating_point_v<U>) {
      std::vector<char> temp(20, 0);
      sprintf(temp.data(), "%f", value);
      param_values.push_back(std::move(temp));
    }
    else if constexpr (std::is_same_v<std::string, U>) {
      std::vector<char> temp = {};
      std::copy(value.data(), value.data() + value.size() + 1,
                std::back_inserter(temp));
      param_values.push_back(std::move(temp));
    }
    else if constexpr (is_char_array_v<U>) {
      std::vector<char> temp = {};
      std::copy(value, value + sizeof(U), std::back_inserter(temp));
      param_values.push_back(std::move(temp));
    }
    else {
      static_assert(!sizeof(U), "this type has not supported yet");
    }
  }

  template <typename T>
  constexpr void assign(T &&value, int row, int i) {
    if (PQgetisnull(res_, row, i) == 1) {
      value = {};
      return;
    }
    using U = std::remove_const_t<std::remove_reference_t<T>>;
    if constexpr (is_optional_v<U>::value) {
      using value_type = typename U::value_type;
      value_type item;
      assign(item, row, i);
      value = std::move(item);
    }
    else if constexpr (std::is_enum_v<U> && !iguana::is_int64_v<U>) {
      value = static_cast<U>(std::atoi(PQgetvalue(res_, row, i)));
    }
    else if constexpr (std::is_integral_v<U> && !iguana::is_int64_v<U>) {
      value = std::atoi(PQgetvalue(res_, row, i));
    }
    else if constexpr (iguana::is_int64_v<U>) {
      value = std::atoll(PQgetvalue(res_, row, i));
    }
    else if constexpr (std::is_floating_point_v<U>) {
      value = std::atof(PQgetvalue(res_, row, i));
    }
    else if constexpr (std::is_same_v<std::string, U>) {
      value = PQgetvalue(res_, row, i);
    }
    else if constexpr (is_char_array_v<U>) {
      auto p = PQgetvalue(res_, row, i);
      memcpy(value, p, sizeof(U));
    }
    else {
      static_assert(!sizeof(U), "this type has not supported yet");
    }
  }

 private:
  struct guard_statment {
    guard_statment(PGresult *res) : res_(res) { reset_error(); }
    ~guard_statment() {
      if (res_ != nullptr) {
        if (PQresultStatus(res_) != PGRES_COMMAND_OK) {
          set_last_error(PQresultErrorMessage(res_));
        }
        PQclear(res_);
      }
    }

   private:
    PGresult *res_ = nullptr;
  };

 private:
  PGconn *con_ = nullptr;
  PGresult *res_ = nullptr;
  inline static bool has_error_ = false;
  inline static std::string last_error_;
};
}  // namespace ormpp
#endif  // ORM_POSTGRESQL_HPP