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

  template <typename Arg, typename... Args>
  bool connect(Arg &&arg, Args &&...) {
    reset_error();
    auto r = sqlite3_open(std::forward<Arg>(arg), &handle_);
    if (r != SQLITE_OK) {
      set_last_error(sqlite3_errmsg(handle_));
      return false;
    }
    return true;
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
  bool delete_records_s(const std::string &str, Args &&...args) {
    auto sql = generate_delete_sql<T>(str);
#ifdef ORMPP_ENABLE_LOG
    std::cout << sql << std::endl;
#endif
    int result = sqlite3_prepare_v2(handle_, sql.data(), (int)sql.size(),
                                    &stmt_, nullptr);
    if (result != SQLITE_OK) {
      set_last_error(sqlite3_errmsg(handle_));
      return false;
    }

    if constexpr (sizeof...(Args) > 0) {
      size_t index = 0;
      (set_param_bind(args, ++index), ...);
    }

    auto guard = guard_statment(stmt_);
    if (sqlite3_step(stmt_) != SQLITE_DONE) {
      set_last_error(sqlite3_errmsg(handle_));
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
      iguana::for_each(t, [this, &t](auto item, auto I) {
        assign(t.*item, (int)decltype(I)::value);
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
      int index = 0;
      iguana::for_each(
          tp,
          [this, &index](auto &item, auto /*I*/) {
            if constexpr (iguana::is_reflection_v<decltype(item)>) {
              std::remove_reference_t<decltype(item)> t = {};
              iguana::for_each(t, [this, &index, &t](auto ele, auto /*i*/) {
                assign(t.*ele, index++);
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
  std::enable_if_t<iguana::is_reflection_v<T>, std::vector<T>> query(
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
      iguana::for_each(t, [this, &t](auto item, auto I) {
        assign(t.*item, (int)decltype(I)::value);
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
      int index = 0;
      iguana::for_each(
          tp,
          [this, &index](auto &item, auto /*I*/) {
            if constexpr (iguana::is_reflection_v<decltype(item)>) {
              std::remove_reference_t<decltype(item)> t = {};
              iguana::for_each(t, [this, &index, &t](auto ele, auto /*i*/) {
                assign(t.*ele, index++);
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
    const auto type_name_arr = get_type_names<T>(DBType::sqlite);
    auto name = get_name<T>();
    std::string sql =
        std::string("CREATE TABLE IF NOT EXISTS ") + name.data() + "(";
    auto arr = iguana::get_array<T>();
    constexpr auto SIZE = sizeof...(Args);

    // auto_increment_key and key can't exist at the same time
    using U = std::tuple<std::decay_t<Args>...>;
    if constexpr (SIZE > 0) {
      // using U = std::tuple<std::decay_t <Args>...>; //the code can't compile
      // in vs2017, why?maybe args... in if constexpr?
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
          [&sql, &i, &has_add_field, &unique_fields, field_name, type_name_arr,
           name, this](auto item) {
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
                append(sql, field_name.data(), " ", type_name_arr[i]);
              }
              append(sql, " PRIMARY KEY AUTOINCREMENT");
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

  template <auto... members, typename T, typename... Args>
  int stmt_execute(const T &t, OptType type, Args &&...args) {
    int index = 1;
    bool bind_ok = true;
    constexpr auto arr = iguana::indexs_of<members...>();
    iguana::for_each(
        t, [&t, arr, &bind_ok, &index, type, this](auto item, auto i) {
          if ((type == OptType::insert &&
               is_auto_key<T>(iguana::get_name<T>(i).data())) ||
              !bind_ok) {
            return;
          }
          if constexpr (sizeof...(members) > 0) {
            for (auto idx : arr) {
              if (idx == decltype(i)::value) {
                bind_ok = set_param_bind(t.*item, index++);
              }
            }
          }
          else {
            bind_ok = set_param_bind(t.*item, index++);
          }
        });

    if constexpr (sizeof...(Args) == 0) {
      if (type == OptType::update) {
        iguana::for_each(t, [&t, &bind_ok, &index, this](auto item, auto i) {
          if (!bind_ok) {
            return;
          }
          if (is_conflict_key<T>(iguana::get_name<T>(i).data())) {
            bind_ok = set_param_bind(t.*item, index++);
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
      return INT_MIN;
    }

    return 1;
  }

  template <typename T>
  bool set_param_bind(T &&value, int i) {
    using U = std::remove_const_t<std::remove_reference_t<T>>;
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
    else if constexpr (std::is_same_v<std::string, U>) {
      return SQLITE_OK ==
             sqlite3_bind_text(stmt_, i, value.data(), value.size(), nullptr);
    }
    else if constexpr (std::is_same_v<char,
                                      std::remove_pointer_t<std::decay_t<U>>>) {
      return SQLITE_OK ==
             sqlite3_bind_text(stmt_, i, value, strlen(value), nullptr);
    }
    else if constexpr (is_char_array_v<U>) {
      return SQLITE_OK ==
             sqlite3_bind_text(stmt_, i, value, sizeof(U), nullptr);
    }
#ifdef ORMPP_WITH_CSTRING
    else if constexpr (std::is_same_v<CString, U>) {
      return SQLITE_OK == sqlite3_bind_text(stmt_, i, value.GetString(),
                                            (int)value.GetLength(), nullptr);
    }
#endif
    else {
      static_assert(!sizeof(U), "this type has not supported yet");
    }
  }

  template <typename T>
  void assign(T &&value, int i) {
    using U = std::remove_const_t<std::remove_reference_t<T>>;
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
    else if constexpr (is_char_array_v<U>) {
      memcpy(value, sqlite3_column_text(stmt_, i), sizeof(U));
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

    if (!begin()) {
      return std::nullopt;
    }

    for (auto &item : v) {
      if (stmt_execute<members...>(item, type, std::forward<Args>(args)...) ==
          INT_MIN) {
        rollback();
        return std::nullopt;
      }

      if (sqlite3_reset(stmt_) != SQLITE_OK) {
        rollback();
        set_last_error(sqlite3_errmsg(handle_));
        return std::nullopt;
      }
    }

    if (!commit()) {
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
  inline static bool has_error_ = false;
  inline static std::string last_error_;
};
}  // namespace ormpp

#endif  // ORM_SQLITE_HPP