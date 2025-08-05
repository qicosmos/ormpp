//
// Created by qiyu on 10/28/17.
//
#include <sqlite3.h>

#include <climits>
#include <string>
#include <vector>

#include "utility.hpp"

#ifndef ORM_SQLITE_HPP
#define ORM_SQLITE_HPP
namespace ormpp {
class sqlite {
 public:
  ~sqlite() { disconnect(); }

  bool has_error() const { return has_error_; }

  static void reset_error() {
    has_error_ = false;
    last_error_ = {};
  }

  static void set_last_error(std::string last_error) {
    has_error_ = true;
    last_error_ = std::move(last_error);
    std::cout << last_error_ << std::endl;  // todo, write to log file
  }

  std::string get_last_error() const { return last_error_; }

  bool connect(
      const std::tuple<std::string, std::string, std::string, std::string,
                       std::optional<int>, std::optional<int>> &tp) {
    reset_error();
    auto r = sqlite3_open(std::get<3>(tp).c_str(), &handle_);
    if (r != SQLITE_OK) {
      set_last_error(sqlite3_errmsg(handle_));
      return false;
    }

#ifdef SQLITE_HAS_CODEC
    // Use password as SQLCipher encryption key if it's not empty
    if (!std::get<2>(tp).empty()) {
      // Use password as key
      std::string key = std::get<2>(tp);
      auto r2 = sqlite3_key(handle_, key.c_str(), key.length());
      if (r2 != SQLITE_OK) {
        set_last_error(sqlite3_errmsg(handle_));
        return false;
      }

      // Test if the key is correct by executing a simple query
      bool can_query = true;
      const char *test_query = "PRAGMA user_version;";
      sqlite3_stmt *stmt = nullptr;
      auto r3 = sqlite3_prepare_v2(handle_, test_query, -1, &stmt, nullptr);
      if (r3 != SQLITE_OK) {
        set_last_error(sqlite3_errmsg(handle_));
        can_query = false;
      }
      else {
        r3 = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        if (r3 != SQLITE_ROW) {
          set_last_error("Invalid SQLCipher key");
          can_query = false;
        }
      }
      if (!can_query) {
        disconnect();
      }
      return can_query;
    }
#endif
    return true;
  }

  bool connect(const std::string &host, const std::string &user,
               const std::string &passwd, const std::string &db,
               const std::optional<int> &timeout,
               const std::optional<int> &port) {
    return connect(std::make_tuple(host, user, passwd.empty() ? user : passwd,
                                   db.empty() ? host : db, timeout, port));
  }

  bool ping() { return true; }

  template <typename... Args>
  bool disconnect(Args &&...args) {
    if (handle_ != nullptr) {
      auto r = sqlite3_close(handle_);
      handle_ = nullptr;
      if (r == SQLITE_OK) {
        return true;
      }
      set_last_error(sqlite3_errmsg(handle_));
      return false;
    }
    return true;
  }

  template <typename T, typename... Args>
  bool create_datatable(Args &&...args) {
    reset_error();
    std::string sql = generate_createtb_sql<T>(std::forward<Args>(args)...);
#ifdef ORMPP_ENABLE_LOG
    std::cout << sql << std::endl;
#endif
    if (sqlite3_exec(handle_, sql.data(), nullptr, nullptr, nullptr) !=
        SQLITE_OK) {
      set_last_error(sqlite3_errmsg(handle_));
      return false;
    }
    return true;
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
  bool delete_records(Args &&...where_conditon) {
    auto sql = generate_delete_sql<T>(std::forward<Args>(where_conditon)...);
    return execute(sql);
  }

  template <typename T, typename... Args>
  uint64_t delete_records_s(const std::string &str, Args &&...args) {
    auto sql = generate_delete_sql<T>(str);
#ifdef ORMPP_ENABLE_LOG
    std::cout << sql << std::endl;
#endif
    int result = sqlite3_prepare_v2(handle_, sql.data(), (int)sql.size(),
                                    &stmt_, nullptr);
    if (result != SQLITE_OK) {
      set_last_error(sqlite3_errmsg(handle_));
      return 0;
    }

    if constexpr (sizeof...(Args) > 0) {
      size_t index = 0;
      (set_param_bind(args, ++index), ...);
    }

    auto guard = guard_statment(stmt_);
    if (sqlite3_step(stmt_) != SQLITE_DONE) {
      set_last_error(sqlite3_errmsg(handle_));
      return 0;
    }
    return sqlite3_changes(handle_);
  }

  template <typename T, typename... Args>
  std::enable_if_t<iguana::ylt_refletable_v<T>, std::vector<T>> query_s(
      const std::string &str, Args &&...args) {
    std::string sql = generate_query_sql<T>(str);
#ifdef ORMPP_ENABLE_LOG
    std::cout << sql << std::endl;
#endif
    int result = sqlite3_prepare_v2(handle_, sql.data(), (int)sql.size(),
                                    &stmt_, nullptr);
    if (result != SQLITE_OK) {
      set_last_error(sqlite3_errmsg(handle_));
      return {};
    }

    if constexpr (sizeof...(Args) > 0) {
      size_t index = 0;
      (set_param_bind(args, ++index), ...);
    }

    auto guard = guard_statment(stmt_);

    std::vector<T> v;
    while (true) {
      result = sqlite3_step(stmt_);
      if (result == SQLITE_DONE)
        break;

      if (result != SQLITE_ROW)
        break;

      T t = {};
      ylt::reflection::for_each(t,
                                [this](auto &field, auto /*name*/, auto index) {
                                  assign(field, index);
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
    int result = sqlite3_prepare_v2(handle_, sql.data(), (int)sql.size(),
                                    &stmt_, nullptr);
    if (result != SQLITE_OK) {
      set_last_error(sqlite3_errmsg(handle_));
      return {};
    }

    if constexpr (sizeof...(Args) > 0) {
      size_t index = 0;
      (set_param_bind(args, ++index), ...);
    }

    auto guard = guard_statment(stmt_);

    std::vector<T> v;
    while (true) {
      result = sqlite3_step(stmt_);
      if (result == SQLITE_DONE)
        break;

      if (result != SQLITE_ROW)
        break;

      T tp = {};
      size_t index = 0;
      ormpp::for_each(
          tp,
          [this, &index](auto &item, auto /*index*/) {
            using U = ylt::reflection::remove_cvref_t<decltype(item)>;
            if constexpr (iguana::ylt_refletable_v<U>) {
              U t = {};
              ylt::reflection::for_each(
                  t,
                  [this, &index](auto &field, auto /*name*/, auto /*index*/) {
                    assign(field, index++);
                  });
              item = std::move(t);
            }
            else {
              assign(item, index++);
            }
          },
          std::make_index_sequence<std::tuple_size_v<T>>{});

      if (index > 0)
        v.push_back(std::move(tp));
    }

    return v;
  }

  // restriction, all the args are string, the first is the where condition,
  // rest are append conditions
  template <typename T, typename... Args>
  std::enable_if_t<iguana::ylt_refletable_v<T>, std::vector<T>> query(
      Args &&...args) {
    std::string sql = generate_query_sql<T>(args...);
#ifdef ORMPP_ENABLE_LOG
    std::cout << sql << std::endl;
#endif
    int result = sqlite3_prepare_v2(handle_, sql.data(), (int)sql.size(),
                                    &stmt_, nullptr);
    if (result != SQLITE_OK) {
      set_last_error(sqlite3_errmsg(handle_));
      return {};
    }

    auto guard = guard_statment(stmt_);

    std::vector<T> v;
    while (true) {
      result = sqlite3_step(stmt_);
      if (result == SQLITE_DONE)
        break;

      if (result != SQLITE_ROW)
        break;

      T t = {};
      ylt::reflection::for_each(t,
                                [this](auto &field, auto /*name*/, auto index) {
                                  assign(field, index);
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
#ifdef ORMPP_ENABLE_LOG
    std::cout << sql << std::endl;
#endif
    constexpr auto Args_Size = sizeof...(Args);
    if constexpr (Args_Size != 0) {
      if (Args_Size != std::count(sql.begin(), sql.end(), '?'))
        return {};

      sql = get_sql(sql, std::forward<Args>(args)...);
    }

    int result = sqlite3_prepare_v2(handle_, sql.data(), (int)sql.size(),
                                    &stmt_, nullptr);
    if (result != SQLITE_OK) {
      set_last_error(sqlite3_errmsg(handle_));
      return {};
    }

    auto guard = guard_statment(stmt_);

    std::vector<T> v;
    while (true) {
      result = sqlite3_step(stmt_);
      if (result == SQLITE_DONE)
        break;

      if (result != SQLITE_ROW)
        break;

      T tp = {};
      size_t index = 0;
      ormpp::for_each(
          tp,
          [this, &index](auto &item, auto /*index*/) {
            using U = ylt::reflection::remove_cvref_t<decltype(item)>;
            if constexpr (iguana::ylt_refletable_v<U>) {
              U t = {};
              ylt::reflection::for_each(
                  t,
                  [this, &index](auto &field, auto /*name*/, auto /*index*/) {
                    assign(field, index++);
                  });
              item = std::move(t);
            }
            else {
              assign(item, index++);
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
    int result = sqlite3_prepare_v2(handle_, sql.data(), (int)sql.size(),
                                    &stmt_, nullptr);
    if (result != SQLITE_OK) {
      set_last_error(sqlite3_errmsg(handle_));
      return false;
    }

    auto guard = guard_statment(stmt_);
    if (sqlite3_step(stmt_) != SQLITE_DONE) {
      set_last_error(sqlite3_errmsg(handle_));
      return false;
    }
    return true;
  }

  int get_last_affect_rows() { return sqlite3_changes(handle_); }

  // transaction
  void set_enable_transaction(bool enable) { transaction_ = enable; }

  bool begin() {
    reset_error();
    if (sqlite3_exec(handle_, "BEGIN", nullptr, nullptr, nullptr) !=
        SQLITE_OK) {
      set_last_error(sqlite3_errmsg(handle_));
      return false;
    }
    return true;
  }

  bool commit() {
    reset_error();
    if (sqlite3_exec(handle_, "COMMIT", nullptr, nullptr, nullptr) !=
        SQLITE_OK) {
      set_last_error(sqlite3_errmsg(handle_));
      return false;
    }
    return true;
  }

  bool rollback() {
    reset_error();
    if (sqlite3_exec(handle_, "ROLLBACK", nullptr, nullptr, nullptr) !=
        SQLITE_OK) {
      set_last_error(sqlite3_errmsg(handle_));
      return false;
    }
    return true;
  }

 private:
  template <typename T, typename... Args>
  std::string generate_createtb_sql(Args &&...args) {
    static const auto type_name_arr = get_type_names<T>(DBType::sqlite);
    auto arr = ylt::reflection::get_member_names<T>();
    constexpr auto SIZE = sizeof...(Args);
    auto name = get_struct_name<T>();
    std::string sql =
        std::string("CREATE TABLE IF NOT EXISTS ") + name.data() + "(";

    // auto_increment_key and key can't exist at the same time
    using U = std::tuple<std::decay_t<Args>...>;
    if constexpr (SIZE > 0) {
      // using U = std::tuple<std::decay_t <Args>...>; //the code can't
      // compile in vs2017, why?maybe args... in if constexpr?
      static_assert(!(iguana::has_type<ormpp_key, U>::value &&
                      iguana::has_type<ormpp_auto_key, U>::value),
                    "should only one key");
    }

    auto tp = sort_tuple(std::make_tuple(std::forward<Args>(args)...));
    const size_t arr_size = arr.size();
    std::set<std::string> unique_fields;
    for (size_t i = 0; i < arr_size; ++i) {
      auto field_name = arr[i];
      bool has_add_field = false;
      for_each0(
          tp,
          [&sql, &i, &has_add_field, &unique_fields, field_name, name,
           this](auto item) {
            if constexpr (std::is_same_v<decltype(item), ormpp_not_null> ||
                          std::is_same_v<decltype(item), ormpp_unique>) {
              if (item.fields.find(std::string(field_name)) ==
                  item.fields.end())
                return;
            }
            else {
              if (item.fields != field_name)
                return;
            }

            if constexpr (std::is_same_v<decltype(item), ormpp_not_null>) {
              if (!has_add_field) {
                append(sql, field_name, " ", type_name_arr[i]);
              }
              append(sql, " NOT NULL");
              has_add_field = true;
            }
            else if constexpr (std::is_same_v<decltype(item), ormpp_key>) {
              if (!has_add_field) {
                append(sql, field_name, " ", type_name_arr[i]);
              }
              append(sql, " PRIMARY KEY ");
              has_add_field = true;
            }
            else if constexpr (std::is_same_v<decltype(item), ormpp_auto_key>) {
              if (!has_add_field) {
                append(sql, field_name, " ", type_name_arr[i]);
              }
              append(sql, " PRIMARY KEY AUTOINCREMENT");
              has_add_field = true;
            }
            else if constexpr (std::is_same_v<decltype(item), ormpp_unique>) {
              unique_fields.insert(std::string(field_name));
            }
            else {
              append(sql, field_name, " ", type_name_arr[i]);
            }
          },
          std::make_index_sequence<SIZE>{});

      if (!has_add_field) {
        append(sql, field_name, " ", type_name_arr[i]);
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

  template <auto... members, typename T, typename... Args>
  int stmt_execute(const T &t, OptType type, Args &&...args) {
    size_t index = 0;
    bool bind_ok = true;
    if constexpr (sizeof...(members) > 0) {
      ((bind_ok &&
            (bind_ok = set_param_bind(
                 ylt::reflection::get<ylt::reflection::index_of<members>()>(t),
                 ++index)),
        true),
       ...);
    }
    else {
      ylt::reflection::for_each(t, [&bind_ok, &index, type, this](
                                       auto &field, auto name, auto /*index*/) {
        if ((type == OptType::insert && is_auto_key<T>(name)) || !bind_ok) {
          return;
        }
        bind_ok = set_param_bind(field, ++index);
      });
    }

    if constexpr (sizeof...(Args) == 0) {
      if (type == OptType::update) {
        ylt::reflection::for_each(
            t,
            [&bind_ok, &index, this](auto &field, auto name, auto /*index*/) {
              if (!bind_ok) {
                return;
              }
              if (is_conflict_key<T>(name)) {
                bind_ok = set_param_bind(field, ++index);
              }
            });
      }
    }

    if (!bind_ok) {
      set_last_error(sqlite3_errmsg(handle_));
      return INT_MIN;
    }

    if (sqlite3_step(stmt_) != SQLITE_DONE) {
      set_last_error(sqlite3_errmsg(handle_));
      return INT_MIN;
    }

    int count = sqlite3_changes(handle_);
    if (count == 0) {
      return type == OptType::update ? count : INT_MIN;
    }

    return 1;
  }

  template <typename T>
  bool set_param_bind(T &&value, int i) {
    using U = ylt::reflection::remove_cvref_t<T>;
    if constexpr (is_optional_v<U>::value) {
      if (value.has_value()) {
        return set_param_bind(std::move(value.value()), i);
      }
      return SQLITE_OK == sqlite3_bind_null(stmt_, i);
    }
    else if constexpr (std::is_enum_v<U> && !iguana::is_int64_v<U>) {
      return SQLITE_OK == sqlite3_bind_int(stmt_, i, static_cast<int>(value));
    }
    else if constexpr (std::is_integral_v<U> && !iguana::is_int64_v<U>) {
      return SQLITE_OK == sqlite3_bind_int(stmt_, i, value);
    }
    else if constexpr (iguana::is_int64_v<U>) {
      return SQLITE_OK == sqlite3_bind_int64(stmt_, i, value);
    }
    else if constexpr (std::is_floating_point_v<U>) {
      return SQLITE_OK == sqlite3_bind_double(stmt_, i, value);
    }
    else if constexpr (iguana::array_v<U> || std::is_same_v<std::string, U>) {
      return SQLITE_OK ==
             sqlite3_bind_text(stmt_, i, value.data(), value.size(), nullptr);
    }
    else if constexpr (iguana::c_array_v<U> ||
                       std::is_same_v<char,
                                      std::remove_pointer_t<std::decay_t<U>>>) {
      return SQLITE_OK ==
             sqlite3_bind_text(stmt_, i, value, strlen(value), nullptr);
    }
    else if constexpr (std::is_same_v<blob, U>) {
      return SQLITE_OK == sqlite3_bind_blob(stmt_, i, value.data(),
                                            static_cast<size_t>(value.size()),
                                            nullptr);
    }
#ifdef ORMPP_WITH_CSTRING
    else if constexpr (std::is_same_v<CString, U>) {
      return SQLITE_OK ==
             sqlite3_bind_text(stmt_, i, value.GetString(),
                               static_cast<size_t>(value.GetLength()), nullptr);
    }
#endif
    else {
      static_assert(!sizeof(U), "this type has not supported yet");
    }
  }

  template <typename T>
  void assign(T &&value, int i) {
    using U = ylt::reflection::remove_cvref_t<T>;
    if (sqlite3_column_type(stmt_, i) == SQLITE_NULL) {
      value = U{};
      return;
    }
    if constexpr (is_optional_v<U>::value) {
      using value_type = typename U::value_type;
      value_type item;
      assign(item, i);
      value = std::move(item);
    }
    else if constexpr (std::is_enum_v<U> && !iguana::is_int64_v<U>) {
      value = static_cast<U>(sqlite3_column_int(stmt_, i));
    }
    else if constexpr (std::is_integral_v<U> && !iguana::is_int64_v<U>) {
      if constexpr (std::is_same_v<U, char>) {
        value = (char)sqlite3_column_int(stmt_, i);
      }
      else {
        value = sqlite3_column_int(stmt_, i);
      }
    }
    else if constexpr (iguana::is_int64_v<U>) {
      value = sqlite3_column_int64(stmt_, i);
    }
    else if constexpr (std::is_floating_point_v<U>) {
      value = sqlite3_column_double(stmt_, i);
    }
    else if constexpr (std::is_same_v<std::string, U>) {
      value.reserve(sqlite3_column_bytes(stmt_, i));
      value.assign((const char *)sqlite3_column_text(stmt_, i),
                   (size_t)sqlite3_column_bytes(stmt_, i));
    }
    else if constexpr (iguana::array_v<U>) {
      memcpy(value.data(), sqlite3_column_text(stmt_, i), sizeof(U));
    }
    else if constexpr (iguana::c_array_v<U>) {
      memcpy(value, sqlite3_column_text(stmt_, i), sizeof(U));
    }
    else if constexpr (std::is_same_v<blob, U>) {
      auto p = (const char *)sqlite3_column_blob(stmt_, i);
      value = blob(p, p + sqlite3_column_bytes(stmt_, i));
    }
#ifdef ORMPP_WITH_CSTRING
    else if constexpr (std::is_same_v<CString, U>) {
      value.SetString((const char *)sqlite3_column_text(stmt_, i));
    }
#endif
    else {
      static_assert(!sizeof(U), "this type has not supported yet");
    }
  }

  template <typename T, typename... Args>
  int insert_impl(OptType type, const T &t, Args &&...args) {
    auto res = insert_or_update_impl(
        t, generate_insert_sql<T>(type == OptType::insert), type);
    return res.has_value() ? res.value() : INT_MIN;
  }

  template <typename T, typename... Args>
  int insert_impl(OptType type, const std::vector<T> &v, Args &&...args) {
    auto res = insert_or_update_impl(
        v, generate_insert_sql<T>(type == OptType::insert), type);
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
#ifdef ORMPP_ENABLE_LOG
    std::cout << sql << std::endl;
#endif
    if (sqlite3_prepare_v2(handle_, sql.data(), (int)sql.size(), &stmt_,
                           nullptr) != SQLITE_OK) {
      set_last_error(sqlite3_errmsg(handle_));
      return std::nullopt;
    }

    auto guard = guard_statment(stmt_);

    if (stmt_execute<members...>(t, type, std::forward<Args>(args)...) ==
        INT_MIN) {
      return std::nullopt;
    }

    return get_insert_id ? sqlite3_last_insert_rowid(handle_) : 1;
  }

  template <auto... members, typename T, typename... Args>
  std::optional<uint64_t> insert_or_update_impl(const std::vector<T> &v,
                                                const std::string &sql,
                                                OptType type,
                                                bool get_insert_id = false,
                                                Args &&...args) {
#ifdef ORMPP_ENABLE_LOG
    std::cout << sql << std::endl;
#endif
    if (sqlite3_prepare_v2(handle_, sql.data(), (int)sql.size(), &stmt_,
                           nullptr) != SQLITE_OK) {
      set_last_error(sqlite3_errmsg(handle_));
      return std::nullopt;
    }

    auto guard = guard_statment(stmt_);

    if (transaction_ && !begin()) {
      return std::nullopt;
    }

    for (auto &item : v) {
      if (stmt_execute<members...>(item, type, std::forward<Args>(args)...) ==
          INT_MIN) {
        if (transaction_) {
          rollback();
        }
        return std::nullopt;
      }

      if (sqlite3_reset(stmt_) != SQLITE_OK) {
        if (transaction_) {
          rollback();
        }
        set_last_error(sqlite3_errmsg(handle_));
        return std::nullopt;
      }
    }

    if (transaction_ && !commit()) {
      return std::nullopt;
    }

    return get_insert_id ? sqlite3_last_insert_rowid(handle_) : (int)v.size();
  }

 private:
  struct guard_statment {
    guard_statment(sqlite3_stmt *stmt) : stmt_(stmt) { reset_error(); }
    ~guard_statment() {
      if (stmt_ != nullptr) {
        auto status = sqlite3_finalize(stmt_);
        if (status) {
          set_last_error("close statment error code " + status);
        }
      }
    }

   private:
    sqlite3_stmt *stmt_ = nullptr;
  };

 private:
  sqlite3 *handle_ = nullptr;
  sqlite3_stmt *stmt_ = nullptr;
  inline static std::string last_error_;
  inline static bool has_error_ = false;
  inline static bool transaction_ = true;
};
}  // namespace ormpp

#endif  // ORM_SQLITE_HPP