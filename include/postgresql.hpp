//
// Created by qiyu on 10/30/17.
//

#ifndef ORM_POSTGRESQL_HPP
#define ORM_POSTGRESQL_HPP

#include <libpq-fe.h>

#include <climits>
#include <string>
#include <type_traits>

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
  constexpr auto create_datatable(Args &&...args) {
    std::string sql = generate_createtb_sql<T>(std::forward<Args>(args)...);
#ifdef ORMPP_ENABLE_LOG
    std::cout << sql << std::endl;
#endif
    res_ = PQexec(con_, sql.data());
    auto guard = guard_statment(res_);
    return PQresultStatus(res_) == PGRES_COMMAND_OK;
  }

  template <typename T, typename... Args>
  constexpr int insert(const T &t, Args &&...args) {
    return insert_or_update(false, t, std::forward<Args>(args)...);
  }

  template <typename T, typename... Args>
  constexpr int insert(const std::vector<T> &v, Args &&...args) {
    return insert_or_update(false, v, std::forward<Args>(args)...);
  }

  template <typename T, typename... Args>
  constexpr int update(const T &t, Args &&...args) {
    return insert_or_update(true, t, std::forward<Args>(args)...);
  }

  template <typename T, typename... Args>
  constexpr int update(const std::vector<T> &v, Args &&...args) {
    return insert_or_update(true, v, std::forward<Args>(args)...);
  }

  template <typename T, typename... Args>
  constexpr std::enable_if_t<iguana::is_reflection_v<T>, std::vector<T>> query(
      Args &&...args) {
    std::string sql = generate_query_sql<T>(args...);
#ifdef ORMPP_ENABLE_LOG
    std::cout << sql << std::endl;
#endif
    constexpr auto SIZE = iguana::get_value<T>();

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
  constexpr std::enable_if_t<!iguana::is_reflection_v<T>, std::vector<T>> query(
      const Arg &s, Args &&...args) {
    static_assert(iguana::is_tuple<T>::value);
    constexpr auto SIZE = std::tuple_size_v<T>;

    std::string sql = s;
#ifdef ORMPP_ENABLE_LOG
    std::cout << sql << std::endl;
#endif
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
              iguana::for_each(t, [this, &index, &t](auto ele, auto i) {
                assign(t.*ele, (int)i, index++);
              });
              item = std::move(t);
            }
            else {
              assign(item, (int)i, index++);
            }
          },
          std::make_index_sequence<SIZE>{});
      v.push_back(std::move(tp));
    }

    return v;
  }

  template <typename T, typename... Args>
  constexpr bool delete_records(Args &&...where_conditon) {
    auto sql = generate_delete_sql<T>(std::forward<Args>(where_conditon)...);
    return execute(sql);
  }

  // just support execute string sql without placeholders
  auto execute(const std::string &sql) {
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
    auto_key_map_[name.data()] = "";
    key_map_[name.data()] = "";

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
                has_add_field = true;
              }
              append(sql, " NOT NULL");
            }
            else if constexpr (std::is_same_v<decltype(item), ormpp_key>) {
              if (!has_add_field) {
                append(sql, field_name.data(), " ", type_name_arr[i]);
                has_add_field = true;
              }
              append(sql, " PRIMARY KEY ");
              key_map_[name.data()] = item.fields;
            }
            else if constexpr (std::is_same_v<decltype(item), ormpp_auto_key>) {
              if (!has_add_field) {
                append(sql, field_name.data(), " ");
                has_add_field = true;
              }
              append(sql, " serial primary key");
              auto_key_map_[name.data()] = item.fields;
              key_map_[name.data()] = item.fields;
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
    res_ =
        PQprepare(con_, "", sql.data(), (int)iguana::get_value<T>(), nullptr);
    auto guard = guard_statment(res_);
    return PQresultStatus(res_) == PGRES_COMMAND_OK;
  }

  template <typename T, typename... Args>
  constexpr int insert_impl(bool update, const T &t, Args &&...args) {
    std::vector<std::vector<char>> param_values;
    auto it = auto_key_map_.find(get_name<T>());
    std::string auto_key =
        (update || it == auto_key_map_.end()) ? "" : it->second;
    iguana::for_each(t, [&t, &param_values, auto_key, this](auto item, auto i) {
      if (!auto_key.empty() &&
          auto_key == iguana::get_name<T>(decltype(i)::value).data()) {
        return;
      }
      set_param_values(param_values, t.*item);
    });

    if (param_values.empty())
      return INT_MIN;

    std::vector<const char *> param_values_buf;
    for (auto &item : param_values) {
      param_values_buf.push_back(item.data());
    }

    res_ = PQexecPrepared(con_, "", (int)param_values.size(),
                          param_values_buf.data(), NULL, NULL, 0);

    auto guard = guard_statment(res_);
    return PQresultStatus(res_) == PGRES_COMMAND_OK ? 1 : INT_MIN;
  }

  template <typename T, typename... Args>
  constexpr int insert_or_update(bool update, const T &t, Args &&...args) {
    std::string sql =
        update ? generate_update_sql<T>(std::forward<Args...>(args)...)
               : generate_auto_insert_sql<T>(auto_key_map_, false);
#ifdef ORMPP_ENABLE_LOG
    std::cout << sql << std::endl;
#endif
    if (!prepare<T>(sql))
      return INT_MIN;

    return insert_impl(update, t, std::forward<Args>(args)...);
  }

  template <typename T, typename... Args>
  constexpr int insert_or_update(bool update, const std::vector<T> &v,
                                 Args &&...args) {
    std::string sql =
        update ? generate_update_sql<T>(std::forward<Args...>(args)...)
               : generate_auto_insert_sql<T>(auto_key_map_, false);
#ifdef ORMPP_ENABLE_LOG
    std::cout << sql << std::endl;
#endif
    if (!begin())
      return INT_MIN;

    if (!prepare<T>(sql))
      return INT_MIN;

    for (auto &item : v) {
      auto result = insert_impl(update, item, std::forward<Args>(args)...);
      if (result == INT_MIN) {
        rollback();
        return INT_MIN;
      }
    }

    if (!commit())
      return INT_MIN;

    return (int)v.size();
  }

  template <typename T, typename... Args>
  inline std::string generate_update_sql(Args &&...args) {
    constexpr auto SIZE = iguana::get_value<T>();
    std::string sql = "insert into ";
    auto name = get_name<T>();
    append(sql, name.data());
    int index = 0;
    std::string set;
    std::string fields = "(";
    std::string values = "values(";
    auto it = key_map_.find(name.data());
    for (auto i = 0; i < SIZE; ++i) {
      std::string field_name = iguana::get_name<T>(i).data();
      std::string value = "$" + std::to_string(++index);
      set += field_name + "=" + value;
      fields += field_name;
      values += value;
      if (i < SIZE - 1) {
        fields += ", ";
        values += ", ";
        set += ", ";
      }
      else {
        fields += ")";
        values += ")";
        set += ";";
      }
    }
    std::string conflict = "on conflict(";
    if constexpr (sizeof...(args) > 0) {
      append(conflict, args...);
    }
    else {
      conflict += it->second;
    }
    conflict += ")";
    append(sql, fields, values, conflict, "do update set", set);
    return sql;
  }

  template <typename T>
  constexpr void set_param_values(std::vector<std::vector<char>> &param_values,
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
    else if constexpr (std::is_integral_v<U> && !iguana::is_int64_v<U>) {
      std::vector<char> temp(20, 0);
      itoa_fwd(value, temp.data());
      param_values.push_back(std::move(temp));
    }
    else if constexpr (iguana::is_int64_v<U>) {
      std::vector<char> temp(65, 0);
      xtoa(value, temp.data(), 10, std::is_signed_v<U>);
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
  inline static std::map<std::string, std::string> key_map_;
  inline static std::map<std::string, std::string> auto_key_map_;
};
}  // namespace ormpp
#endif  // ORM_POSTGRESQL_HPP