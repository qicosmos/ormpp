//
// Created by qiyu on 10/20/17.
//

#ifndef ORM_MYSQL_HPP
#define ORM_MYSQL_HPP
#include <climits>
#include <list>
#include <map>
#include <string_view>
#include <utility>

#include "entity.hpp"
#include "type_mapping.hpp"
#include "utility.hpp"

namespace ormpp {

using blob = ormpp_mysql::blob;

class mysql {
 public:
  ~mysql() { disconnect(); }

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

  template <typename... Args>
  bool connect(Args &&...args) {
    reset_error();
    if (con_ != nullptr) {
      mysql_close(con_);
    }

    con_ = mysql_init(nullptr);
    if (!con_) {
      set_last_error("mysql init failed");
      return false;
    }

    int timeout = -1;
    auto tp = get_tp(timeout, std::forward<Args>(args)...);

    if (timeout > 0) {
      if (mysql_options(con_, MYSQL_OPT_CONNECT_TIMEOUT, &timeout) != 0) {
        set_last_error(mysql_error(con_));
        return false;
      }
    }

    char value = 1;
    mysql_options(con_, MYSQL_OPT_RECONNECT, &value);
    mysql_options(con_, MYSQL_SET_CHARSET_NAME, "utf8");

    if (std::apply(&mysql_real_connect, tp) == nullptr) {
      set_last_error(mysql_error(con_));
      return false;
    }

    return true;
  }

  bool ping() { return mysql_ping(con_) == 0; }

  template <typename... Args>
  bool disconnect(Args &&...args) {
    if (con_ != nullptr) {
      mysql_close(con_);
      con_ = nullptr;
    }
    return true;
  }

  template <typename T, typename... Args>
  bool create_datatable(Args &&...args) {
    reset_error();
    std::string sql = generate_createtb_sql<T>(std::forward<Args>(args)...);
    sql += " DEFAULT CHARSET=utf8";
#ifdef ORMPP_ENABLE_LOG
    std::cout << sql << std::endl;
#endif
    if (mysql_query(con_, sql.data())) {
      set_last_error(mysql_error(con_));
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

  int get_last_affect_rows() { return (int)mysql_affected_rows(con_); }

  template <typename T>
  constexpr void set_param_bind(std::vector<MYSQL_BIND> &param_binds,
                                T &&value) {
    MYSQL_BIND param = {};

    using U = std::remove_const_t<std::remove_reference_t<T>>;
    if constexpr (is_optional_v<U>::value) {
      if (value.has_value()) {
        return set_param_bind(param_binds, std::move(value.value()));
      }
      else {
        param.buffer_type = MYSQL_TYPE_NULL;
      }
    }
    else if constexpr (std::is_enum_v<U>) {
      param.buffer_type = MYSQL_TYPE_LONG;
      param.buffer = const_cast<void *>(static_cast<const void *>(&value));
    }
    else if constexpr (std::is_arithmetic_v<U>) {
      if constexpr (std::is_same_v<bool, U>) {
        param.buffer_type = MYSQL_TYPE_TINY;
      }
      else {
        param.buffer_type =
            (enum_field_types)ormpp_mysql::type_to_id(identity<U>{});
      }
      param.buffer = const_cast<void *>(static_cast<const void *>(&value));
    }
    else if constexpr (std::is_same_v<std::string, U>) {
      param.buffer_type = MYSQL_TYPE_STRING;
      param.buffer = (void *)(value.c_str());
      param.buffer_length = (unsigned long)value.size();
    }
    else if constexpr (std::is_same_v<const char *, U> || is_char_array_v<U>) {
      param.buffer_type = MYSQL_TYPE_STRING;
      param.buffer = (void *)(value);
      param.buffer_length = (unsigned long)strlen(value);
    }
    else if constexpr (std::is_same_v<blob, U>) {
      param.buffer_type = MYSQL_TYPE_BLOB;
      param.buffer = (void *)(value.data());
      param.buffer_length = (unsigned long)value.size();
    }
    else {
      static_assert(!sizeof(U), "this type has not supported yet");
    }
    param_binds.push_back(param);
  }

  template <typename T, typename B>
  void set_param_bind(MYSQL_BIND &param_bind, T &&value, int i,
                      std::map<size_t, std::vector<char>> &mp, B &is_null) {
    using U = std::remove_const_t<std::remove_reference_t<T>>;
    if constexpr (is_optional_v<U>::value) {
      return set_param_bind(param_bind, *value, i, mp, is_null);
    }
    else if constexpr (std::is_enum_v<U>) {
      param_bind.buffer_type = MYSQL_TYPE_LONG;
      param_bind.buffer = const_cast<void *>(static_cast<const void *>(&value));
    }
    else if constexpr (std::is_arithmetic_v<U>) {
      if constexpr (std::is_same_v<bool, U>) {
        param_bind.buffer_type = MYSQL_TYPE_TINY;
      }
      else {
        param_bind.buffer_type =
            (enum_field_types)ormpp_mysql::type_to_id(identity<U>{});
      }
      param_bind.buffer = const_cast<void *>(static_cast<const void *>(&value));
    }
    else if constexpr (std::is_same_v<std::string, U>) {
      param_bind.buffer_type = MYSQL_TYPE_STRING;
      std::vector<char> tmp(65536, 0);
      mp.emplace(i, std::move(tmp));
      param_bind.buffer = &(mp.rbegin()->second[0]);
      param_bind.buffer_length = 65536;
    }
    else if constexpr (is_char_array_v<U>) {
      param_bind.buffer_type = MYSQL_TYPE_VAR_STRING;
      std::vector<char> tmp(sizeof(U), 0);
      mp.emplace(i, std::move(tmp));
      param_bind.buffer = &(mp.rbegin()->second[0]);
      param_bind.buffer_length = (unsigned long)sizeof(U);
    }
    else if constexpr (std::is_same_v<blob, U>) {
      param_bind.buffer_type = MYSQL_TYPE_BLOB;
      std::vector<char> tmp(65536, 0);
      mp.emplace(i, std::move(tmp));
      param_bind.buffer = &(mp.rbegin()->second[0]);
      param_bind.buffer_length = 65536;
    }
    else {
      static_assert(!sizeof(U), "this type has not supported yet");
    }
    param_bind.is_null = (B)&is_null;
  }

  template <typename T>
  void set_value(MYSQL_BIND &param_bind, T &&value, int i,
                 std::map<size_t, std::vector<char>> &mp) {
    using U = std::remove_const_t<std::remove_reference_t<T>>;
    if constexpr (is_optional_v<U>::value) {
      using value_type = typename U::value_type;
      if constexpr (std::is_arithmetic_v<value_type>) {
        value_type item;
        memcpy(&item, param_bind.buffer, sizeof(value_type));
        value = std::move(item);
      }
      else {
        value_type item;
        value = std::move(item);
        return set_value(param_bind, *value, i, mp);
      }
    }
    else if constexpr (std::is_same_v<std::string, U>) {
      auto &vec = mp[i];
      value = std::string(&vec[0], strlen(vec.data()));
    }
    else if constexpr (is_char_array_v<U>) {
      auto &vec = mp[i];
      memcpy(value, vec.data(), vec.size());
    }
    else if constexpr (std::is_same_v<blob, U>) {
      auto &vec = mp[i];
      value = blob(vec.data(), vec.data() + get_blob_len(i));
    }
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
    stmt_ = mysql_stmt_init(con_);
    if (!stmt_) {
      set_last_error(mysql_error(con_));
      return false;
    }

    auto guard = guard_statment(stmt_);
    if (mysql_stmt_prepare(stmt_, sql.c_str(), (unsigned long)sql.size())) {
      set_last_error(mysql_stmt_error(stmt_));
      return false;
    }

    if constexpr (sizeof...(Args) > 0) {
      size_t index = 0;
      std::vector<MYSQL_BIND> param_binds;
      (set_param_bind(param_binds, args), ...);
      if (mysql_stmt_bind_param(stmt_, &param_binds[0])) {
        set_last_error(mysql_stmt_error(stmt_));
        return false;
      }
    }

    if (mysql_stmt_execute(stmt_)) {
      set_last_error(mysql_stmt_error(stmt_));
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
    constexpr auto SIZE = iguana::get_value<T>();

    stmt_ = mysql_stmt_init(con_);
    if (!stmt_) {
      set_last_error(mysql_error(con_));
      return {};
    }

    auto guard = guard_statment(stmt_);

    if (mysql_stmt_prepare(stmt_, sql.c_str(), (unsigned long)sql.size())) {
      set_last_error(mysql_stmt_error(stmt_));
      return {};
    }

    if constexpr (sizeof...(Args) > 0) {
      size_t index = 0;
      std::vector<MYSQL_BIND> param_binds;
      (set_param_bind(param_binds, args), ...);
      if (mysql_stmt_bind_param(stmt_, &param_binds[0])) {
        set_last_error(mysql_stmt_error(stmt_));
        return {};
      }
    }

    std::array<decltype(std::declval<MYSQL_BIND>().is_null), SIZE> nulls = {};
    std::array<MYSQL_BIND, SIZE> param_binds = {};
    std::map<size_t, std::vector<char>> mp;

    std::vector<T> v;
    T t{};

    int index = 0;
    iguana::for_each(t, [&param_binds, &index, &nulls, &mp, &t, this](
                            auto item, auto /*i*/) {
      set_param_bind(param_binds[index], t.*item, index, mp, nulls[index]);
      index++;
    });

    if (index == 0) {
      return {};
    }

    if (mysql_stmt_bind_result(stmt_, &param_binds[0])) {
      set_last_error(mysql_stmt_error(stmt_));
      return {};
    }

    if (mysql_stmt_execute(stmt_)) {
      set_last_error(mysql_stmt_error(stmt_));
      return {};
    }

    while (mysql_stmt_fetch(stmt_) == 0) {
      iguana::for_each(t, [&param_binds, &mp, &t, this](auto item, auto i) {
        constexpr auto Idx = decltype(i)::value;
        set_value(param_binds.at(Idx), t.*item, Idx, mp);
      });

      for (auto &p : mp) {
        p.second.assign(p.second.size(), 0);
      }

      iguana::for_each(t, [nulls, &t](auto item, auto i) {
        constexpr auto Idx = decltype(i)::value;
        if (nulls.at(Idx)) {
          using U = std::remove_reference_t<decltype(std::declval<T>().*item)>;
          if constexpr (is_optional_v<U>::value || std::is_arithmetic_v<U>) {
            t.*item = {};
          }
        }
      });

      v.push_back(std::move(t));
    }

    return v;
  }

  template <typename T, typename... Args>
  std::enable_if_t<!iguana::is_reflection_v<T>, std::vector<T>> query_s(
      const std::string &sql, Args &&...args) {
    static_assert(iguana::is_tuple<T>::value);
    constexpr auto SIZE = std::tuple_size_v<T>;
#ifdef ORMPP_ENABLE_LOG
    std::cout << sql << std::endl;
#endif
    stmt_ = mysql_stmt_init(con_);
    if (!stmt_) {
      set_last_error(mysql_error(con_));
      return {};
    }

    auto guard = guard_statment(stmt_);

    if (mysql_stmt_prepare(stmt_, sql.c_str(), (int)sql.size())) {
      set_last_error(mysql_stmt_error(stmt_));
      return {};
    }

    if constexpr (sizeof...(Args) > 0) {
      size_t index = 0;
      std::vector<MYSQL_BIND> param_binds;
      (set_param_bind(param_binds, args), ...);
      if (mysql_stmt_bind_param(stmt_, &param_binds[0])) {
        set_last_error(mysql_stmt_error(stmt_));
        return {};
      }
    }

    std::array<decltype(std::declval<MYSQL_BIND>().is_null),
               result_size<T>::value>
        nulls = {};
    std::array<MYSQL_BIND, result_size<T>::value> param_binds = {};
    std::map<size_t, std::vector<char>> mp;

    std::vector<T> v;
    T tp{};

    int index = 0;
    iguana::for_each(
        tp,
        [&param_binds, &index, &nulls, &mp, &tp, this](auto &t, auto /*i*/) {
          using U = std::remove_reference_t<decltype(t)>;
          if constexpr (iguana::is_reflection_v<U>) {
            iguana::for_each(t, [&param_binds, &index, &nulls, &mp, &t, this](
                                    auto &item, auto /*i*/) {
              set_param_bind(param_binds[index], t.*item, index, mp,
                             nulls[index]);
              index++;
            });
          }
          else {
            set_param_bind(param_binds[index], t, index, mp, nulls[index]);
            index++;
          }
        },
        std::make_index_sequence<std::tuple_size_v<T>>{});

    if (index == 0) {
      return {};
    }

    if (mysql_stmt_bind_result(stmt_, &param_binds[0])) {
      set_last_error(mysql_stmt_error(stmt_));
      return {};
    }

    if (mysql_stmt_execute(stmt_)) {
      set_last_error(mysql_stmt_error(stmt_));
      return {};
    }

    while (mysql_stmt_fetch(stmt_) == 0) {
      index = 0;
      iguana::for_each(
          tp,
          [&param_binds, &index, &mp, &tp, this](auto &t, auto /*i*/) {
            using U = std::remove_reference_t<decltype(t)>;
            if constexpr (iguana::is_reflection_v<U>) {
              iguana::for_each(t, [&param_binds, &index, &mp, &t, this](
                                      auto ele, auto /*i*/) {
                set_value(param_binds.at(index), t.*ele, index, mp);
                index++;
              });
            }
            else {
              set_value(param_binds.at(index), t, index, mp);
              index++;
            }
          },
          std::make_index_sequence<SIZE>{});

      for (auto &p : mp) {
        p.second.assign(p.second.size(), 0);
      }

      index = 0;
      iguana::for_each(
          tp,
          [&index, nulls, &tp](auto &t, auto /*i*/) {
            using U = std::remove_reference_t<decltype(t)>;
            if constexpr (iguana::is_reflection_v<U>) {
              iguana::for_each(t, [&index, nulls, &t](auto ele, auto /*i*/) {
                if (nulls.at(index++)) {
                  using W =
                      std::remove_reference_t<decltype(std::declval<U>().*ele)>;
                  if constexpr (is_optional_v<W>::value ||
                                std::is_arithmetic_v<W>) {
                    t.*ele = {};
                  }
                }
              });
            }
            else {
              if (nulls.at(index++)) {
                if constexpr (is_optional_v<U>::value ||
                              std::is_arithmetic_v<U>) {
                  t = {};
                }
              }
            }
          },
          std::make_index_sequence<SIZE>{});

      v.push_back(std::move(tp));
    }

    return v;
  }

  // if there is a sql error, how to tell the user? throw exception?
  template <typename T, typename... Args>
  std::enable_if_t<iguana::is_reflection_v<T>, std::vector<T>> query(
      Args &&...args) {
    std::string sql = generate_query_sql<T>(args...);
#ifdef ORMPP_ENABLE_LOG
    std::cout << sql << std::endl;
#endif
    constexpr auto SIZE = iguana::get_value<T>();

    stmt_ = mysql_stmt_init(con_);
    if (!stmt_) {
      set_last_error(mysql_error(con_));
      return {};
    }

    auto guard = guard_statment(stmt_);

    if (mysql_stmt_prepare(stmt_, sql.c_str(), (unsigned long)sql.size())) {
      set_last_error(mysql_stmt_error(stmt_));
      return {};
    }

    std::array<decltype(std::declval<MYSQL_BIND>().is_null), SIZE> nulls = {};
    std::array<MYSQL_BIND, SIZE> param_binds = {};
    std::map<size_t, std::vector<char>> mp;

    std::vector<T> v;
    T t{};

    int index = 0;
    iguana::for_each(t, [&param_binds, &index, &nulls, &mp, &t, this](
                            auto item, auto /*i*/) {
      set_param_bind(param_binds[index], t.*item, index, mp, nulls[index]);
      index++;
    });

    if (index == 0) {
      return {};
    }

    if (mysql_stmt_bind_result(stmt_, &param_binds[0])) {
      set_last_error(mysql_stmt_error(stmt_));
      return {};
    }

    if (mysql_stmt_execute(stmt_)) {
      set_last_error(mysql_stmt_error(stmt_));
      return {};
    }

    while (mysql_stmt_fetch(stmt_) == 0) {
      iguana::for_each(t, [&param_binds, &mp, &t, this](auto item, auto i) {
        constexpr auto Idx = decltype(i)::value;
        set_value(param_binds.at(Idx), t.*item, Idx, mp);
      });

      for (auto &p : mp) {
        p.second.assign(p.second.size(), 0);
      }

      iguana::for_each(t, [nulls, &t](auto item, auto i) {
        constexpr auto Idx = decltype(i)::value;
        if (nulls.at(Idx)) {
          using U = std::remove_reference_t<decltype(std::declval<T>().*item)>;
          if constexpr (is_optional_v<U>::value || std::is_arithmetic_v<U>) {
            t.*item = {};
          }
        }
      });

      v.push_back(std::move(t));
    }

    return v;
  }

  // for tuple and string with args...
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
      if (Args_Size != std::count(sql.begin(), sql.end(), '?')) {
        set_last_error("arg size error");
        return {};
      }

      sql = get_sql(sql, std::forward<Args>(args)...);
    }

    stmt_ = mysql_stmt_init(con_);
    if (!stmt_) {
      set_last_error(mysql_error(con_));
      return {};
    }

    auto guard = guard_statment(stmt_);

    if (mysql_stmt_prepare(stmt_, sql.c_str(), (int)sql.size())) {
      set_last_error(mysql_stmt_error(stmt_));
      return {};
    }

    std::array<decltype(std::declval<MYSQL_BIND>().is_null),
               result_size<T>::value>
        nulls = {};
    std::array<MYSQL_BIND, result_size<T>::value> param_binds = {};
    std::map<size_t, std::vector<char>> mp;

    std::vector<T> v;
    T tp{};

    int index = 0;
    iguana::for_each(
        tp,
        [&param_binds, &index, &nulls, &mp, &tp, this](auto &t, auto /*i*/) {
          using U = std::remove_reference_t<decltype(t)>;
          if constexpr (iguana::is_reflection_v<U>) {
            iguana::for_each(t, [&param_binds, &index, &nulls, &mp, &t, this](
                                    auto &item, auto /*i*/) {
              set_param_bind(param_binds[index], t.*item, index, mp,
                             nulls[index]);
              index++;
            });
          }
          else {
            set_param_bind(param_binds[index], t, index, mp, nulls[index]);
            index++;
          }
        },
        std::make_index_sequence<SIZE>{});

    if (index == 0) {
      return {};
    }

    if (mysql_stmt_bind_result(stmt_, &param_binds[0])) {
      set_last_error(mysql_stmt_error(stmt_));
      return {};
    }

    if (mysql_stmt_execute(stmt_)) {
      set_last_error(mysql_stmt_error(stmt_));
      return {};
    }

    while (mysql_stmt_fetch(stmt_) == 0) {
      index = 0;
      iguana::for_each(
          tp,
          [&param_binds, &index, &mp, &tp, this](auto &t, auto /*i*/) {
            using U = std::remove_reference_t<decltype(t)>;
            if constexpr (iguana::is_reflection_v<U>) {
              iguana::for_each(t, [&param_binds, &index, &mp, &t, this](
                                      auto ele, auto /*i*/) {
                set_value(param_binds.at(index), t.*ele, index, mp);
                index++;
              });
            }
            else {
              set_value(param_binds.at(index), t, index, mp);
              index++;
            }
          },
          std::make_index_sequence<SIZE>{});

      for (auto &p : mp) {
        p.second.assign(p.second.size(), 0);
      }

      index = 0;
      iguana::for_each(
          tp,
          [&index, nulls, &tp](auto &t, auto /*i*/) {
            using U = std::remove_reference_t<decltype(t)>;
            if constexpr (iguana::is_reflection_v<U>) {
              iguana::for_each(t, [&index, nulls, &t](auto ele, auto /*i*/) {
                if (nulls.at(index++)) {
                  using W =
                      std::remove_reference_t<decltype(std::declval<U>().*ele)>;
                  if constexpr (is_optional_v<W>::value ||
                                std::is_arithmetic_v<W>) {
                    t.*ele = {};
                  }
                }
              });
            }
            else {
              if (nulls.at(index++)) {
                if constexpr (is_optional_v<U>::value ||
                              std::is_arithmetic_v<U>) {
                  t = {};
                }
              }
            }
          },
          std::make_index_sequence<SIZE>{});

      v.push_back(std::move(tp));
    }

    return v;
  }

  int get_blob_len(int column) {
    reset_error();
    unsigned long data_len = 0;

    MYSQL_BIND param;
    memset(&param, 0, sizeof(MYSQL_BIND));
    param.length = &data_len;
    param.buffer_type = MYSQL_TYPE_BLOB;

    auto retcode = mysql_stmt_fetch_column(stmt_, &param, column, 0);
    if (retcode != 0) {
      set_last_error(mysql_stmt_error(stmt_));
      return 0;
    }

    return static_cast<int>(data_len);
  }

  // just support execute string sql without placeholders
  bool execute(const std::string &sql) {
#ifdef ORMPP_ENABLE_LOG
    std::cout << sql << std::endl;
#endif
    stmt_ = mysql_stmt_init(con_);
    if (!stmt_) {
      set_last_error(mysql_error(con_));
      return false;
    }

    auto guard = guard_statment(stmt_);
    if (mysql_stmt_prepare(stmt_, sql.c_str(), (unsigned long)sql.size())) {
      set_last_error(mysql_stmt_error(stmt_));
      return false;
    }

    if (mysql_stmt_execute(stmt_)) {
      set_last_error(mysql_stmt_error(stmt_));
      return false;
    }
    return true;
  }

  // transaction
  bool begin() {
    reset_error();
    if (mysql_query(con_, "BEGIN")) {
      set_last_error(mysql_error(con_));
      return false;
    }
    return true;
  }

  bool commit() {
    reset_error();
    if (mysql_query(con_, "COMMIT")) {
      set_last_error(mysql_error(con_));
      return false;
    }
    return true;
  }

  bool rollback() {
    reset_error();
    if (mysql_query(con_, "ROLLBACK")) {
      set_last_error(mysql_error(con_));
      return false;
    }
    return true;
  }

 private:
  template <typename T, typename... Args>
  std::string generate_createtb_sql(Args &&...args) {
    const auto type_name_arr = get_type_names<T>(DBType::mysql);
    auto name = get_name<T>();
    std::string sql =
        std::string("CREATE TABLE IF NOT EXISTS ") + name.data() + "(";
    auto arr = iguana::get_array<T>();
    constexpr auto SIZE = sizeof...(Args);

    // auto_increment_key and key can't exist at the same time
    using U = std::tuple<std::decay_t<Args>...>;
    if constexpr (SIZE > 0) {
      // using U = std::tuple<std::decay_t <Args>...>;//the code can't compile
      // in vs
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
              append(sql, " PRIMARY KEY");
              has_add_field = true;
            }
            else if constexpr (std::is_same_v<decltype(item), ormpp_auto_key>) {
              if (!has_add_field) {
                append(sql, field_name.data(), " ", type_name_arr[i]);
              }
              append(sql, " AUTO_INCREMENT");
              append(sql, " PRIMARY KEY");
              has_add_field = true;
            }
            else if constexpr (std::is_same_v<decltype(item), ormpp_unique>) {
              if (!has_add_field) {
                if (type_name_arr[i] == "TEXT") {
                  append(sql, field_name.data(), " ", "varchar(512)");
                }
                else {
                  append(sql, field_name.data(), " ", type_name_arr[i]);
                }
              }
              unique_fields.insert(field_name.data());
              has_add_field = true;
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
    std::vector<MYSQL_BIND> param_binds;
    constexpr auto arr = iguana::indexs_of<members...>();
    iguana::for_each(t, [&t, arr, &param_binds, type, this](auto item, auto i) {
      if (type == OptType::insert &&
          is_auto_key<T>(iguana::get_name<T>(i).data())) {
        return;
      }
      if constexpr (sizeof...(members) > 0) {
        for (auto idx : arr) {
          if (idx == decltype(i)::value) {
            set_param_bind(param_binds, t.*item);
          }
        }
      }
      else {
        set_param_bind(param_binds, t.*item);
      }
    });

    if constexpr (sizeof...(Args) == 0) {
      if (type == OptType::update) {
        iguana::for_each(t, [&t, &param_binds, this](auto item, auto i) {
          std::string field_name = "`";
          field_name += iguana::get_name<T>(i).data();
          field_name += "`";
          if (is_conflict_key<T>(field_name)) {
            set_param_bind(param_binds, t.*item);
          }
        });
      }
    }

    if (mysql_stmt_bind_param(stmt_, &param_binds[0])) {
      set_last_error(mysql_stmt_error(stmt_));
      return INT_MIN;
    }

    if (mysql_stmt_execute(stmt_)) {
      set_last_error(mysql_stmt_error(stmt_));
      return INT_MIN;
    }

    int count = (int)mysql_stmt_affected_rows(stmt_);
    if (count == 0) {
      return INT_MIN;
    }

    return count;
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
    stmt_ = mysql_stmt_init(con_);
    if (!stmt_) {
      set_last_error(mysql_error(con_));
      return std::nullopt;
    }

    if (mysql_stmt_prepare(stmt_, sql.c_str(), (int)sql.size())) {
      set_last_error(mysql_stmt_error(stmt_));
      return std::nullopt;
    }

    auto guard = guard_statment(stmt_);

    if (stmt_execute<members...>(t, type, std::forward<Args>(args)...) ==
        INT_MIN) {
      return std::nullopt;
    }

    return get_insert_id ? stmt_->mysql->insert_id : 1;
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
    stmt_ = mysql_stmt_init(con_);
    if (!stmt_) {
      set_last_error(mysql_error(con_));
      return std::nullopt;
    }

    if (mysql_stmt_prepare(stmt_, sql.c_str(), (int)sql.size())) {
      set_last_error(mysql_stmt_error(stmt_));
      return std::nullopt;
    }

    auto guard = guard_statment(stmt_);

    if (!get_insert_id && !begin()) {
      return std::nullopt;
    }

    for (auto &item : v) {
      if (stmt_execute<members...>(item, type, std::forward<Args>(args)...) ==
          INT_MIN) {
        rollback();
        return std::nullopt;
      }
    }

    if (!get_insert_id && !commit()) {
      return std::nullopt;
    }

    return get_insert_id ? stmt_->mysql->insert_id : (int)v.size();
  }

  template <typename... Args>
  auto get_tp(int &timeout, Args &&...args) {
    auto tp = std::make_tuple(con_, std::forward<Args>(args)...);
    if constexpr (sizeof...(Args) == 5) {
      auto [c, s1, s2, s3, s4, i] = tp;
      timeout = i;
      return std::make_tuple(c, s1, s2, s3, s4, 0, nullptr, 0);
    }
    else if constexpr (sizeof...(Args) == 6) {
      auto [c, s1, s2, s3, s4, i, port] = tp;
      timeout = i;
      return std::make_tuple(c, s1, s2, s3, s4, port, nullptr, 0);
    }
    else {
      return std::tuple_cat(tp, std::make_tuple(0, nullptr, 0));
    }
  }

 private:
  struct guard_statment {
    guard_statment(MYSQL_STMT *stmt) : stmt_(stmt) { reset_error(); }
    ~guard_statment() {
      if (stmt_ != nullptr) {
        auto status = mysql_stmt_close(stmt_);
        if (status) {
          set_last_error("close statment error code " + status);
        }
      }
    }

   private:
    MYSQL_STMT *stmt_ = nullptr;
  };

 private:
  MYSQL *con_ = nullptr;
  MYSQL_STMT *stmt_ = nullptr;
  inline static bool has_error_ = false;
  inline static std::string last_error_;
};
}  // namespace ormpp

#endif  // ORM_MYSQL_HPP
