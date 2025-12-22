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
  bool connect(
      const std::tuple<std::string, std::string, std::string, std::string,
                       std::optional<int>, std::optional<int>> &tp) {
    reset_error();
    auto sql = generate_conn_sql(tp);
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

  bool connect(const std::string &host, const std::string &user,
               const std::string &passwd, const std::string &db,
               const std::optional<int> &timeout,
               const std::optional<int> &port) {
    return connect(std::make_tuple(host, user, passwd, db, timeout, port));
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
  uint64_t delete_records_s(const std::string &str, Args &&...args) {
    auto sql = generate_delete_sql<T>(str);
#ifdef ORMPP_ENABLE_LOG
    std::cout << sql << std::endl;
#endif
    if (!prepare<T>(sql))
      return 0;

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
      return 0;
    }
    return std::strtoull(PQcmdTuples(res_), nullptr, 10);
  }

  template <typename T, typename... Args>
  std::enable_if_t<iguana::ylt_refletable_v<T>, std::vector<T>> query_s(
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
      ylt::reflection::for_each(
          t, [this, i](auto &field, auto /*name*/, auto index) {
            assign(field, i, index);
          });
      v.push_back(std::move(t));
    }

    return v;
  }

  template <typename T, typename... Args>
  std::enable_if_t<iguana::non_ylt_refletable_v<T>, std::vector<T>> query_s(
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
      size_t index = 0;
      ormpp::for_each(
          tp,
          [this, i, &index](auto &item, auto /*index*/) {
            using U = ylt::reflection::remove_cvref_t<decltype(item)>;
            if constexpr (iguana::ylt_refletable_v<U>) {
              U t = {};
              ylt::reflection::for_each(
                  t, [this, &index, &t, i](auto &field, auto /*name*/,
                                           auto /*index*/) {
                    assign(field, (int)i, index++);
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
  std::enable_if_t<iguana::ylt_refletable_v<T>, std::vector<T>> query(
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
      ylt::reflection::for_each(
          t, [this, i](auto &field, auto /*name*/, auto index) {
            assign(field, i, index);
          });
      v.push_back(std::move(t));
    }

    return v;
  }

  template <typename T, typename Arg, typename... Args>
  std::enable_if_t<iguana::non_ylt_refletable_v<T>, std::vector<T>> query(
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
      ormpp::for_each(
          tp,
          [this, i, &index](auto &item, auto /*index*/) {
            using U = ylt::reflection::remove_cvref_t<decltype(item)>;
            if constexpr (iguana::ylt_refletable_v<U>) {
              U t = {};
              ylt::reflection::for_each(
                  t, [this, &index, i](auto &field, auto /*name*/,
                                       auto /*index*/) {
                    assign(field, (int)i, index++);
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
  void set_enable_transaction(bool enable) { transaction_ = enable; }

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
  std::string generate_conn_sql(
      const std::tuple<std::string, std::string, std::string, std::string,
                       std::optional<int>, std::optional<int>> &tp) {
    std::string params;
    params.append("host=").append(std::get<0>(tp)).append(" ");
    params.append("user=").append(std::get<1>(tp)).append(" ");
    params.append("password=").append(std::get<2>(tp)).append(" ");
    params.append("dbname=").append(std::get<3>(tp)).append(" ");
    if (std::get<4>(tp).has_value()) {
      params.append("connect_timeout=")
          .append(std::to_string(std::get<4>(tp).value()))
          .append(" ");
    }
    if (std::get<5>(tp).has_value()) {
      params.append("port=")
          .append(std::to_string(std::get<5>(tp).value()))
          .append(" ");
    }
    params.pop_back();
    return params;
  }

  template <typename T, typename... Args>
  std::string generate_createtb_sql(Args &&...args) {
    std::set<std::string> not_null;
    std::set<std::string> unique;
    std::set<std::string> auto_primary_key;
    std::set<std::string> primary_keys;

    std::string_view auto_key = get_auto_key<T>();
    if (!auto_key.empty()) {
      auto_primary_key.insert(std::string(auto_key));
    }

    // 宏定义的conflict keys作为联合主键，优先级比ormpp_key更高
    auto pks = get_conflict_keys<T>();
    if (!pks.empty()) {
      for (auto &key : pks) {
        primary_keys.insert(key);
      }
    }

    if constexpr (sizeof...(Args) > 0) {
      ylt::reflection::for_each(std::make_tuple(args...), [&](auto &item) {
        using U = std::decay_t<decltype(item)>;
        if constexpr (std::is_same_v<ormpp_auto_key, U>) {
          auto_primary_key.insert(item.fields);
        }
        else if constexpr (std::is_same_v<ormpp_key, U>) {
          if (pks.empty())
            primary_keys.insert(item.fields);
        }
        else if constexpr (std::is_same_v<ormpp_not_null, U>) {
          for (auto &name : item.fields) {
            not_null.insert(name);
          }
        }
        else if constexpr (std::is_same_v<ormpp_unique, U>) {
          if (item.fields.size() > 1) {
            std::string str;
            for (auto &name : item.fields) {
              str.append(name).append(",");
            }
            str.pop_back();
            unique.insert(str);
          }
          else {
            unique.insert(*item.fields.begin());
          }
        }
      });
    }

    auto table_name = ylt::reflection::get_struct_name<T>();
    const auto type_name_arr = get_type_names<T>(DBType::postgresql);

    std::string sql;
    sql.append("CREATE TABLE IF NOT EXISTS ").append(table_name).append("(");
    T t;
    ylt::reflection::for_each(t, [&](auto &field, auto name, size_t index) {
      using item_type = std::decay_t<decltype(field)>;
      std::string type_str = type_name_arr[index];
      sql.append(name).append(" ").append(type_str);

      std::string str_name(name);

      if (!auto_primary_key.empty() &&
          auto_primary_key.find(str_name) != auto_primary_key.end()) {
        // remove additional type str for auto key
        for (int i = 0; i < type_str.size(); i++) {
          sql.pop_back();
        }
        if (type_str == "bigint") {
          sql.append(" bigserial ");
        }
        else {
          sql.append(" serial ");
        }
        auto_key = name;
        auto_primary_key.clear();
      }
      else if (!not_null.empty() && not_null.find(str_name) != not_null.end()) {
        sql.append(" NOT NULL");
        not_null.erase(str_name);
      }

      sql.append(",");
    });

    if (!auto_key.empty()) {
      sql.append("PRIMARY KEY (").append(auto_key).append("),");
    }
    else if (!primary_keys.empty()) {
      sql.append("PRIMARY KEY (");
      for (auto key : primary_keys) {
        sql.append(key).append(",");
      }
      sql.pop_back();
      sql.append("),");
    }

    for (auto &name : unique) {
      sql.append("UNIQUE (").append(name).append("),");
    }
    sql.pop_back();
    sql.append(")");

    return sql;
  }

  template <typename T>
  bool prepare(const std::string &sql) {
#ifdef ORMPP_ENABLE_LOG
    std::cout << sql << std::endl;
#endif
    res_ = PQprepare(con_, "", sql.data(), ylt::reflection::members_count_v<T>,
                     nullptr);
    auto guard = guard_statment(res_);
    return PQresultStatus(res_) == PGRES_COMMAND_OK;
  }

  template <auto... members, typename T, typename... Args>
  std::optional<uint64_t> stmt_execute(const T &t, OptType type,
                                       Args &&...args) {
    std::vector<std::vector<char>> param_values;
    constexpr auto arr = indexs_of<members...>();
    if constexpr (sizeof...(members) > 0) {
      (set_param_values(
           param_values,
           ylt::reflection::get<ylt::reflection::index_of<members>()>(t)),
       ...);
    }
    else {
      ylt::reflection::for_each(t, [arr, &param_values, type, this](
                                       auto &field, auto name, auto index) {
        if (type == OptType::insert && is_auto_key<T>(name)) {
          return;
        }
        if constexpr (sizeof...(members) > 0) {
          for (auto idx : arr) {
            if (idx == index) {
              set_param_values(param_values, field);
            }
          }
        }
        else {
          set_param_values(param_values, field);
        }
      });
    }

    if constexpr (sizeof...(Args) == 0) {
      if (type == OptType::update) {
        ylt::reflection::for_each(
            t, [&param_values, this](auto &field, auto name, auto /*index*/) {
              if (is_conflict_key<T>(name)) {
                set_param_values(param_values, field);
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
    auto res = insert_or_update_impl<members...>(
        t, generate_update_sql<T, members...>(std::forward<Args>(args)...),
        OptType::update, false, std::forward<Args>(args)...);
    return res.has_value() ? res.value() : INT_MIN;
  }

  template <auto... members, typename T, typename... Args>
  int update_impl(const std::vector<T> &v, Args &&...args) {
    auto res = insert_or_update_impl<members...>(
        v, generate_update_sql<T, members...>(std::forward<Args>(args)...),
        OptType::update, false, std::forward<Args>(args)...);
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
    if (transaction_ && !begin()) {
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
        if (transaction_) {
          rollback();
        }
        return std::nullopt;
      }
    }

    if (transaction_ && !commit()) {
      return std::nullopt;
    }

    return get_insert_id ? res : (int)v.size();
  }

  template <typename T>
  void set_param_values(std::vector<std::vector<char>> &param_values,
                        T &&value) {
    using U = ylt::reflection::remove_cvref_t<T>;
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
    else if constexpr (iguana::array_v<U> || std::is_same_v<std::string, U> ||
                       std::is_same_v<std::string_view, U>) {
      std::vector<char> temp = {};
      std::copy(value.data(), value.data() + value.size() + 1,
                std::back_inserter(temp));
      param_values.push_back(std::move(temp));
    }
    else if constexpr (iguana::c_array_v<U>) {
      std::vector<char> temp = {};
      std::copy(value, value + sizeof(U), std::back_inserter(temp));
      param_values.push_back(std::move(temp));
    }
    else if constexpr (std::is_same_v<blob, U>) {
      std::vector<char> temp = {};
      std::copy(value.data(), value.data() + value.size(),
                std::back_inserter(temp));
      param_values.push_back(std::move(temp));
    }
#ifdef ORMPP_WITH_CSTRING
    else if constexpr (std::is_same_v<CString, U>) {
      std::vector<char> temp = {};
      std::copy(value.GetString(), value.GetString() + value.GetLength() + 1,
                std::back_inserter(temp));
      param_values.push_back(std::move(temp));
    }
#endif
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
    using U = ylt::reflection::remove_cvref_t<T>;
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
    else if constexpr (std::is_same_v<std::string_view, U>) {
      sv_ = PQgetvalue(res_, row, i);
      value = sv_;
    }
    else if constexpr (iguana::array_v<U>) {
      auto p = PQgetvalue(res_, row, i);
      memcpy(value.data(), p, value.size());
    }
    else if constexpr (iguana::c_array_v<U>) {
      auto p = PQgetvalue(res_, row, i);
      memcpy(value, p, sizeof(U));
    }
    else if constexpr (std::is_same_v<blob, U>) {
      auto p = PQgetvalue(res_, row, i);
      value = blob(p, p + PQgetlength(res_, row, i));
    }
#ifdef ORMPP_WITH_CSTRING
    else if constexpr (std::is_same_v<CString, U>) {
      value.SetString(PQgetvalue(res_, row, i));
    }
#endif
    else {
      static_assert(!sizeof(U), "this type has not supported yet");
    }
  }

 private:
  struct guard_statment {
    guard_statment(PGresult *res) : res_(res) { reset_error(); }
    ~guard_statment() {
      if (res_ != nullptr) {
        auto status = PQresultStatus(res_);
        if (status != PGRES_COMMAND_OK && status != PGRES_TUPLES_OK) {
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
  inline static std::string sv_;
  inline static std::string last_error_;
  inline static bool has_error_ = false;
  inline static bool transaction_ = true;
};
}  // namespace ormpp
#endif  // ORM_POSTGRESQL_HPP
