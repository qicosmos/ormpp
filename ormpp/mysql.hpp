//
// Created by qiyu on 10/20/17.
//

#ifndef ORM_MYSQL_HPP
#define ORM_MYSQL_HPP

#include <algorithm>
#include <climits>
#include <list>
#include <map>
#include <string_view>
#include <utility>

#include "entity.hpp"
#include "query.hpp"
#include "type_mapping.hpp"

namespace ormpp {

class mysql {
 public:
  static constexpr DBType db_type_v = DBType::mysql;
  using mysql_null_flag_t =
      std::remove_pointer_t<decltype(std::declval<MYSQL_BIND>().is_null)>;

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

  bool connect(
      const std::tuple<std::string, std::string, std::string, std::string,
                       std::optional<int>, std::optional<int>> &tp) {
    reset_error();
    if (con_ != nullptr) {
      mysql_close(con_);
    }

    con_ = mysql_init(nullptr);
    if (!con_) {
      set_last_error("mysql init failed");
      return false;
    }

    int timeout = std::get<4>(tp).has_value() ? std::get<4>(tp).value() : -1;

    if (timeout > 0) {
      if (mysql_options(con_, MYSQL_OPT_CONNECT_TIMEOUT, &timeout) != 0) {
        set_last_error(mysql_error(con_));
        return false;
      }
    }

    mysql_options(con_, MYSQL_SET_CHARSET_NAME, "utf8mb4");

    if (mysql_real_connect(
            con_, std::get<0>(tp).c_str(), std::get<1>(tp).c_str(),
            std::get<2>(tp).c_str(), std::get<3>(tp).c_str(),
            std::get<5>(tp).has_value() ? std::get<5>(tp).value() : 0, nullptr,
            0) == nullptr) {
      set_last_error(mysql_error(con_));
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
    sql += " DEFAULT CHARSET=utf8mb4";
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
    auto res = insert_or_update_impl(t, generate_insert_sql<T>(db_type_v, true),
                                     OptType::insert, true);
    return res.has_value() ? res.value() : 0;
  }

  template <typename T, typename... Args>
  uint64_t get_insert_id_after_insert(const std::vector<T> &v, Args &&...args) {
    auto res = insert_or_update_impl(v, generate_insert_sql<T>(db_type_v, true),
                                     OptType::insert, true);
    return res.has_value() ? res.value() : 0;
  }

  int get_last_affect_rows() { return last_affect_rows_; }

  template <typename T>
  void set_param_bind(std::vector<MYSQL_BIND> &param_binds, T &&value) {
    MYSQL_BIND param = {};
    using U = ylt::reflection::remove_cvref_t<T>;
    if constexpr (is_optional_v<U>::value) {
      if (value.has_value()) {
        return set_param_bind(param_binds, value.value());
      }
      else {
        param.buffer_type = MYSQL_TYPE_NULL;
      }
    }
    else if constexpr (std::is_enum_v<U>) {
      using enum_type = std::underlying_type_t<U>;
      if constexpr (std::is_integral_v<enum_type>) {
        param.is_unsigned = std::is_unsigned_v<enum_type>;
      }
      param.buffer_type =
          (enum_field_types)ormpp_mysql::type_to_id(identity<enum_type>{});
      param.buffer_length = sizeof(enum_type);
      enum_type item = static_cast<enum_type>(value);
      std::vector<char> tmp(sizeof(enum_type), 0);
      memcpy(tmp.data(), &item, sizeof(enum_type));
      input_bind_buffers_.emplace_back(std::move(tmp));
      param.buffer = input_bind_buffers_.back().data();
    }
    else if constexpr (std::is_arithmetic_v<U>) {
      if constexpr (std::is_same_v<bool, U>) {
        param.buffer_type = MYSQL_TYPE_TINY;
      }
      else {
        if constexpr (std::is_integral_v<U>) {
          param.is_unsigned = std::is_unsigned_v<U>;
        }
        param.buffer_type =
            (enum_field_types)ormpp_mysql::type_to_id(identity<U>{});
      }
      param.buffer = const_cast<void *>(static_cast<const void *>(&value));
    }
    else if constexpr (std::is_same_v<std::string, U> ||
                       std::is_same_v<std::string_view, U>) {
      param.buffer_type = MYSQL_TYPE_STRING;
      param.buffer = (void *)(value.data());
      param.buffer_length = (unsigned long)value.size();
    }
    else if constexpr (iguana::array_v<U>) {
      param.buffer_type = MYSQL_TYPE_STRING;
      param.buffer = (void *)(value.data());
      param.buffer_length =
          (std::min)(std::strlen(value.data()), (size_t)value.size());
    }
    else if constexpr (iguana::c_array_v<U> ||
                       std::is_same_v<const char *, U>) {
      param.buffer_type = MYSQL_TYPE_STRING;
      param.buffer = (void *)(value);
      param.buffer_length = (unsigned long)strlen(value);
    }
    else if constexpr (std::is_same_v<blob, U>) {
      param.buffer_type = MYSQL_TYPE_BLOB;
      param.buffer = (void *)(value.data());
      param.buffer_length = (unsigned long)value.size();
    }
#ifdef ORMPP_WITH_CSTRING
    else if constexpr (std::is_same_v<CString, U>) {
      param.buffer_type = MYSQL_TYPE_STRING;
      param.buffer = (void *)(value.GetString());
      param.buffer_length = (unsigned long)value.GetLength();
    }
#endif
    else {
      static_assert(!sizeof(U), "this type has not supported yet");
    }
    param_binds.push_back(param);
  }

  template <typename T>
  void set_param_bind(MYSQL_RES *meta_, MYSQL_BIND &param_bind, T &&value,
                      int i, std::map<size_t, std::vector<char>> &mp,
                      mysql_null_flag_t &is_null) {
    using U = ylt::reflection::remove_cvref_t<T>;

    if constexpr (is_optional_v<U>::value) {
      using value_type = typename U::value_type;
      if (!value.has_value()) {
        value = value_type{};
      }
      return set_param_bind(meta_, param_bind, *value, i, mp, is_null);
    }
    else if constexpr (std::is_enum_v<U>) {
      using enum_type = std::underlying_type_t<U>;
      if constexpr (std::is_integral_v<enum_type>) {
        param_bind.is_unsigned = std::is_unsigned_v<enum_type>;
      }
      param_bind.buffer_type =
          (enum_field_types)ormpp_mysql::type_to_id(identity<enum_type>{});
      param_bind.buffer_length = sizeof(enum_type);
      std::vector<char> tmp(param_bind.buffer_length, 0);
      auto [it, _] = mp.emplace(i, std::move(tmp));
      param_bind.buffer = it->second.data();
    }
    else if constexpr (std::is_arithmetic_v<U>) {
      if constexpr (std::is_same_v<bool, U>) {
        param_bind.buffer_type = MYSQL_TYPE_TINY;
      }
      else {
        if constexpr (std::is_integral_v<U>) {
          param_bind.is_unsigned = std::is_unsigned_v<U>;
        }
        param_bind.buffer_type =
            (enum_field_types)ormpp_mysql::type_to_id(identity<U>{});
      }
      param_bind.buffer = const_cast<void *>(static_cast<const void *>(&value));
      param_bind.buffer_length = sizeof(U);
    }
    else if constexpr (std::is_same_v<std::string, U> ||
                       std::is_same_v<std::string_view, U>) {
      unsigned long buffer_size = 256;
      enum_field_types buffer_type = MYSQL_TYPE_STRING;

      MYSQL_FIELD *field = mysql_fetch_field_direct(meta_, i);
      if (field) {
        if (field->type == MYSQL_TYPE_MEDIUM_BLOB ||
            field->type == MYSQL_TYPE_LONG_BLOB) {
          buffer_type = field->type;
        }
        buffer_size = field->length + 1;
      }

      param_bind.buffer_type = buffer_type;
      std::vector<char> tmp(buffer_size, 0);
      auto [it, _] = mp.emplace(i, std::move(tmp));
      param_bind.buffer = it->second.data();
      param_bind.buffer_length = buffer_size;
    }
    else if constexpr (iguana::array_v<U>) {
      param_bind.buffer_type = MYSQL_TYPE_VAR_STRING;
      std::vector<char> tmp(sizeof(U), 0);
      auto [it, _] = mp.emplace(i, std::move(tmp));
      param_bind.buffer = it->second.data();
      param_bind.buffer_length = (unsigned long)sizeof(U);
    }
    else if constexpr (std::is_same_v<blob, U>) {
      unsigned long buffer_size = 65536;
      enum_field_types buffer_type = MYSQL_TYPE_BLOB;

      MYSQL_FIELD *field = mysql_fetch_field_direct(meta_, i);
      if (field) {
        buffer_type = field->type;
        buffer_size = field->length;
      }

      param_bind.buffer_type = buffer_type;
      std::vector<char> tmp(buffer_size, 0);
      auto [it, _] = mp.emplace(i, std::move(tmp));
      param_bind.buffer = it->second.data();
      param_bind.buffer_length = buffer_size;
    }
#ifdef ORMPP_WITH_CSTRING
    else if constexpr (std::is_same_v<CString, U>) {
      unsigned long buffer_size = 256;
      enum_field_types buffer_type = MYSQL_TYPE_STRING;

      MYSQL_FIELD *field = mysql_fetch_field_direct(meta_, i);
      if (field) {
        if (field->type == MYSQL_TYPE_MEDIUM_BLOB ||
            field->type == MYSQL_TYPE_LONG_BLOB) {
          buffer_type = field->type;
        }
        buffer_size = field->length + 1;
      }

      param_bind.buffer_type = buffer_type;
      std::vector<char> tmp(buffer_size, 0);
      auto [it, _] = mp.emplace(i, std::move(tmp));
      param_bind.buffer = it->second.data();
      param_bind.buffer_length = buffer_size;
    }
#endif
    else {
      static_assert(!sizeof(U), "this type has not supported yet");
    }
    param_bind.is_null = &is_null;
  }

  template <size_t N>
  bool fetch_truncated_columns(std::array<MYSQL_BIND, N> &result_binds,
                               std::array<unsigned long, N> &lengths,
                               std::map<size_t, std::vector<char>> &mp) {
    for (size_t i = 0; i < N; ++i) {
      if (lengths[i] <= result_binds[i].buffer_length) {
        continue;
      }

      auto it = mp.find(i);
      if (it == mp.end()) {
        set_last_error("mysql_stmt_fetch data truncated at column " +
                       std::to_string(i));
        return false;
      }

      it->second.assign(lengths[i] + 1, 0);
      result_binds[i].buffer = it->second.data();
      result_binds[i].buffer_length = lengths[i] + 1;
      if (mysql_stmt_fetch_column(stmt_, &result_binds[i],
                                  static_cast<unsigned int>(i), 0)) {
        set_last_error(std::string(mysql_stmt_error(stmt_)) + " at column " +
                       std::to_string(i));
        return false;
      }
    }

    return true;
  }

  template <typename T>
  void set_value(MYSQL_BIND &param_bind, T &&value, int i,
                 std::map<size_t, std::vector<char>> &mp) {
    using U = ylt::reflection::remove_cvref_t<T>;
    auto result_length = [&param_bind]() {
      return param_bind.length == nullptr ? param_bind.buffer_length
                                          : *param_bind.length;
    };
    if (param_bind.is_null != nullptr && *param_bind.is_null != 0) {
      if constexpr (is_optional_v<U>::value) {
        value = std::nullopt;
      }
      else if constexpr (std::is_default_constructible_v<U> &&
                         std::is_assignable_v<decltype(value), U>) {
        value = {};
      }
      return;
    }
    if constexpr (is_optional_v<U>::value) {
      using value_type = typename U::value_type;
      if constexpr (std::is_arithmetic_v<value_type>) {
        if (param_bind.buffer == nullptr ||
            param_bind.buffer_length < sizeof(value_type)) {
          set_last_error("mysql result buffer is invalid at column " +
                         std::to_string(i));
          return;
        }
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
    else if constexpr (std::is_enum_v<U> || std::is_arithmetic_v<U>) {
      size_t value_size = sizeof(U);
      if constexpr (std::is_enum_v<U>) {
        value_size = sizeof(std::underlying_type_t<U>);
      }
      if (param_bind.buffer == nullptr ||
          param_bind.buffer_length < value_size) {
        set_last_error("mysql result buffer is invalid at column " +
                       std::to_string(i));
        return;
      }
      if (param_bind.length != nullptr && *param_bind.length < value_size) {
        set_last_error("mysql result length is invalid at column " +
                       std::to_string(i));
        return;
      }
      if constexpr (std::is_enum_v<U>) {
        using enum_type = std::underlying_type_t<U>;
        enum_type item;
        memcpy(&item, param_bind.buffer, sizeof(enum_type));
        value = static_cast<U>(item);
      }
      else {
        U item;
        memcpy(&item, param_bind.buffer, sizeof(U));
        value = item;
      }
    }
    else if constexpr (std::is_same_v<std::string, U>) {
      auto it = mp.find(i);
      if (it == mp.end()) {
        set_last_error("mysql result buffer is missing at column " +
                       std::to_string(i));
        return;
      }
      auto &vec = it->second;
      auto len = result_length();
      if (len > vec.size()) {
        set_last_error("mysql result length exceeds buffer at column " +
                       std::to_string(i));
        return;
      }
      value = std::string(vec.data(), len);
    }
    else if constexpr (std::is_same_v<std::string_view, U>) {
      auto it = mp.find(i);
      if (it == mp.end()) {
        set_last_error("mysql result buffer is missing at column " +
                       std::to_string(i));
        return;
      }
      auto &vec = it->second;
      auto len = result_length();
      if (len > vec.size()) {
        set_last_error("mysql result length exceeds buffer at column " +
                       std::to_string(i));
        return;
      }
      sv_ = std::string(vec.data(), len);
      value = sv_;
    }
    else if constexpr (iguana::array_v<U>) {
      auto it = mp.find(i);
      if (it == mp.end()) {
        set_last_error("mysql result buffer is missing at column " +
                       std::to_string(i));
        return;
      }
      auto &vec = it->second;
      auto len = result_length();
      if (len > value.size() || len > vec.size()) {
        set_last_error("mysql result length exceeds buffer at column " +
                       std::to_string(i));
        return;
      }
      std::fill(value.begin(), value.end(), 0);
      memcpy(value.data(), vec.data(), len);
    }
    else if constexpr (std::is_same_v<blob, U>) {
      auto it = mp.find(i);
      if (it == mp.end()) {
        set_last_error("mysql result buffer is missing at column " +
                       std::to_string(i));
        return;
      }
      auto &vec = it->second;
      auto len =
          param_bind.length == nullptr ? get_blob_len(i) : result_length();
      if (len > vec.size()) {
        set_last_error("mysql result length exceeds buffer at column " +
                       std::to_string(i));
        return;
      }
      value = blob(vec.data(), vec.data() + len);
    }
#ifdef ORMPP_WITH_CSTRING
    else if constexpr (std::is_same_v<CString, U>) {
      auto it = mp.find(i);
      if (it == mp.end()) {
        set_last_error("mysql result buffer is missing at column " +
                       std::to_string(i));
        return;
      }
      auto &vec = it->second;
      auto len = result_length();
      if (len > vec.size()) {
        set_last_error("mysql result length exceeds buffer at column " +
                       std::to_string(i));
        return;
      }
      auto str = std::string(vec.data(), len);
      value.SetString(str.c_str());
    }
#endif
  }

  template <typename T, typename... Args>
  bool delete_records(Args &&...where_conditon) {
    auto sql = generate_delete_sql<T>(db_type_v,
                                      std::forward<Args>(where_conditon)...);
    return execute(sql);
  }

  template <typename T, typename... Args>
  uint64_t delete_records_s(const std::string &str, Args &&...args) {
    auto sql = generate_delete_sql<T>(db_type_v, str);
#ifdef ORMPP_ENABLE_LOG
    std::cout << sql << std::endl;
#endif
    stmt_ = mysql_stmt_init(con_);
    if (!stmt_) {
      set_last_error(mysql_error(con_));
      return 0;
    }

    auto guard = guard_statment(stmt_);
    if (mysql_stmt_prepare(stmt_, sql.c_str(), (unsigned long)sql.size())) {
      set_last_error(mysql_stmt_error(stmt_));
      return 0;
    }

    if constexpr (sizeof...(Args) > 0) {
      size_t index = 0;
      std::vector<MYSQL_BIND> param_binds;
      input_bind_buffers_.clear();
      (set_param_bind(param_binds, args), ...);
      if (mysql_stmt_bind_param(stmt_, &param_binds[0])) {
        set_last_error(mysql_stmt_error(stmt_));
        return 0;
      }
    }

    if (mysql_stmt_execute(stmt_)) {
      set_last_error(mysql_stmt_error(stmt_));
      return 0;
    }
    return (uint64_t)mysql_stmt_affected_rows(stmt_);
  }

  template <typename T, typename... Args>
  std::enable_if_t<iguana::ylt_refletable_v<T>, std::vector<T>> query_s(
      const std::string &str, Args &&...args) {
    constexpr auto SIZE = ylt::reflection::members_count_v<T>;
    std::string sql =
        contains_select(str) ? str : generate_query_sql<T>(db_type_v, str);
#ifdef ORMPP_ENABLE_LOG
    std::cout << sql << std::endl;
#endif

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

    meta_ = mysql_stmt_result_metadata(stmt_);
    if (!meta_) {
      set_last_error(mysql_stmt_error(stmt_));
      return {};
    }

    auto meta_guard = guard_result(meta_);

    if constexpr (sizeof...(Args) > 0) {
      std::vector<MYSQL_BIND> param_binds;
      input_bind_buffers_.clear();
      (set_param_bind(param_binds, args), ...);
      if (mysql_stmt_bind_param(stmt_, &param_binds[0])) {
        set_last_error(mysql_stmt_error(stmt_));
        return {};
      }
    }

    std::array<mysql_null_flag_t, SIZE> nulls = {};
    std::array<unsigned long, SIZE> lengths = {};
    std::array<MYSQL_BIND, SIZE> param_binds = {};
    std::map<size_t, std::vector<char>> mp;

    T t{};
    size_t index = 0;
    std::vector<T> v;
    ylt::reflection::for_each(
        t, [&param_binds, &index, &nulls, &lengths, &mp, this](
               auto &field, auto /*name*/, auto /*index*/) {
          set_param_bind(this->meta_, param_binds[index], field, index, mp,
                         nulls[index]);
          param_binds[index].length = &lengths[index];
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

    int fetch_ret = 0;
    while ((fetch_ret = mysql_stmt_fetch(stmt_)) == 0 ||
           fetch_ret == MYSQL_DATA_TRUNCATED) {
      ylt::reflection::for_each(
          t, [&param_binds, &mp, this](auto &field, auto /*name*/, auto index) {
            set_value(param_binds.at(index), field, index, mp);
          });

      for (auto &p : mp) {
        p.second.assign(p.second.size(), 0);
      }

      ylt::reflection::for_each(t, [nulls](auto &field, auto /*name*/,
                                           auto index) {
        if (nulls.at(index)) {
          using U = ylt::reflection::remove_cvref_t<decltype(field)>;
          if constexpr (is_optional_v<U>::value || std::is_arithmetic_v<U>) {
            field = {};
          }
        }
      });

      v.push_back(std::move(t));
    }

    return v;
  }

  template <typename T, typename... Args>
  std::enable_if_t<iguana::non_ylt_refletable_v<T>, std::vector<T>> query_s(
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

    meta_ = mysql_stmt_result_metadata(stmt_);
    if (!meta_) {
      set_last_error(mysql_stmt_error(stmt_));
      return {};
    }

    auto meta_guard = guard_result(meta_);

    if constexpr (sizeof...(Args) > 0) {
      size_t index = 0;
      std::vector<MYSQL_BIND> param_binds;
      input_bind_buffers_.clear();
      (set_param_bind(param_binds, args), ...);
      if (mysql_stmt_bind_param(stmt_, &param_binds[0])) {
        set_last_error(mysql_stmt_error(stmt_));
        return {};
      }
    }

    std::array<mysql_null_flag_t, result_size<T>::value> nulls = {};
    std::array<unsigned long, result_size<T>::value> lengths = {};
    std::array<MYSQL_BIND, result_size<T>::value> param_binds = {};
    std::map<size_t, std::vector<char>> mp;

    T tp{};
    size_t index = 0;
    std::vector<T> v;
    ormpp::for_each(
        tp,
        [&param_binds, &index, &nulls, &lengths, &mp, this](auto &item,
                                                            auto /*index*/) {
          using U = ylt::reflection::remove_cvref_t<decltype(item)>;
          if constexpr (iguana::ylt_refletable_v<U>) {
            ylt::reflection::for_each(
                item, [&param_binds, &index, &nulls, &lengths, &mp, this](
                          auto &field, auto /*name*/, auto /*index*/) {
                  set_param_bind(this->meta_, param_binds[index], field, index,
                                 mp, nulls[index]);
                  param_binds[index].length = &lengths[index];
                  index++;
                });
          }
          else {
            set_param_bind(this->meta_, param_binds[index], item, index, mp,
                           nulls[index]);
            param_binds[index].length = &lengths[index];
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

    int fetch_ret2 = 0;
    while ((fetch_ret2 = mysql_stmt_fetch(stmt_)) == 0 ||
           fetch_ret2 == MYSQL_DATA_TRUNCATED) {
      index = 0;
      ormpp::for_each(
          tp,
          [&param_binds, &index, &mp, this](auto &item, auto /*index*/) {
            using U = ylt::reflection::remove_cvref_t<decltype(item)>;
            if constexpr (iguana::ylt_refletable_v<U>) {
              ylt::reflection::for_each(
                  item, [&param_binds, &index, &mp, this](
                            auto &field, auto /*name*/, auto /*index*/) {
                    set_value(param_binds.at(index), field, index, mp);
                    index++;
                  });
            }
            else {
              set_value(param_binds.at(index), item, index, mp);
              index++;
            }
          },
          std::make_index_sequence<SIZE>{});

      for (auto &p : mp) {
        p.second.assign(p.second.size(), 0);
      }

      index = 0;
      ormpp::for_each(
          tp,
          [&index, nulls](auto &item, auto /*index*/) {
            using U = ylt::reflection::remove_cvref_t<decltype(item)>;
            if constexpr (iguana::ylt_refletable_v<U>) {
              ylt::reflection::for_each(item, [&index, nulls](auto &field,
                                                              auto /*name*/,
                                                              auto /*index*/) {
                if (nulls.at(index++)) {
                  using W = ylt::reflection::remove_cvref_t<decltype(field)>;
                  if constexpr (is_optional_v<W>::value ||
                                std::is_arithmetic_v<W>) {
                    field = {};
                  }
                }
              });
            }
            else {
              if (nulls.at(index++)) {
                if constexpr (is_optional_v<U>::value ||
                              std::is_arithmetic_v<U>) {
                  item = {};
                }
              }
            }
          },
          std::make_index_sequence<SIZE>{});

      v.push_back(std::move(tp));
    }

    return v;
  }

  template <typename T, typename... Args>
    requires valid_query_each_args_v<T, Args...>
  std::enable_if_t<iguana::ylt_refletable_v<T>, uint64_t> query_each(
      const std::string &str, Args &&...args) {
    reset_error();
    check_query_each_args<T, Args...>();
    constexpr auto SIZE = ylt::reflection::members_count_v<T>;
    std::string sql =
        contains_select(str) ? str : generate_query_sql<T>(db_type_v, str);
#ifdef ORMPP_ENABLE_LOG
    std::cout << sql << std::endl;
#endif

    stmt_ = mysql_stmt_init(con_);
    if (!stmt_) {
      set_last_error(mysql_error(con_));
      return 0;
    }

    auto guard = guard_statment(stmt_);

    if (mysql_stmt_prepare(stmt_, sql.c_str(), (unsigned long)sql.size())) {
      set_last_error(mysql_stmt_error(stmt_));
      return 0;
    }

    auto params = std::forward_as_tuple(std::forward<Args>(args)...);
    static_assert(sizeof...(Args) >= 1,
                  "query_each requires a callback as the last argument");
    constexpr size_t param_count = sizeof...(Args) - 1;
    auto bind_params =
        decay_tuple_prefix(params, std::make_index_sequence<param_count>{});
    std::vector<MYSQL_BIND> input_binds;
    if constexpr (param_count > 0) {
      input_bind_buffers_.clear();
      for_each_tuple_prefix(
          bind_params,
          [this, &input_binds](auto &&arg) {
            set_param_bind(input_binds, arg);
          },
          std::make_index_sequence<param_count>{});
      if (mysql_stmt_bind_param(stmt_, input_binds.data())) {
        set_last_error(mysql_stmt_error(stmt_));
        return 0;
      }
    }

    if (mysql_stmt_execute(stmt_)) {
      set_last_error(mysql_stmt_error(stmt_));
      return 0;
    }

    meta_ = mysql_stmt_result_metadata(stmt_);
    if (!meta_) {
      set_last_error(mysql_stmt_error(stmt_));
      return 0;
    }

    auto meta_guard = guard_result(meta_);
    if (mysql_num_fields(meta_) != SIZE) {
      set_last_error("query_each result column count mismatch");
      return 0;
    }

    auto &&callback = std::get<param_count>(params);
    std::array<mysql_null_flag_t, SIZE> nulls = {};
    std::array<unsigned long, SIZE> lengths = {};
    std::array<MYSQL_BIND, SIZE> result_binds = {};
    std::map<size_t, std::vector<char>> mp;
    T bind_row{};
    size_t index = 0;
    ylt::reflection::for_each(
        bind_row, [&result_binds, &index, &nulls, &lengths, &mp, this](
                      auto &field, auto /*name*/, auto /*index*/) {
          set_param_bind(this->meta_, result_binds[index], field, index, mp,
                         nulls[index]);
          result_binds[index].length = &lengths[index];
          index++;
        });

    if (index == 0) {
      set_last_error("query_each result bind count is zero");
      return 0;
    }

    if (mysql_stmt_bind_result(stmt_, result_binds.data())) {
      set_last_error(mysql_stmt_error(stmt_));
      return 0;
    }

    uint64_t count = 0;
    int fetch_ret = 0;
    bool stopped = false;
    while (true) {
      fetch_ret = mysql_stmt_fetch(stmt_);
      if (fetch_ret == MYSQL_DATA_TRUNCATED) {
        if (!fetch_truncated_columns(result_binds, lengths, mp)) {
          return count;
        }
      }
      else if (fetch_ret != 0) {
        break;
      }
      T t{};
      ylt::reflection::for_each(t, [&result_binds, &mp, &nulls, this](
                                       auto &field, auto /*name*/, auto index) {
        if (nulls.at(index) != 0) {
          using U = ylt::reflection::remove_cvref_t<decltype(field)>;
          if constexpr (std::is_default_constructible_v<U> &&
                        std::is_assignable_v<decltype(field), U>) {
            field = {};
          }
          return;
        }
        set_value(result_binds.at(index), field, index, mp);
      });

      ++count;
      if (!invoke_query_each_callback(callback, t)) {
        stopped = true;
        break;
      }
    }

    if (!stopped && fetch_ret != MYSQL_NO_DATA) {
      if (fetch_ret == MYSQL_DATA_TRUNCATED) {
        set_last_error("query_each mysql_stmt_fetch data truncated");
      }
      else {
        set_last_error(mysql_stmt_error(stmt_));
      }
    }

    return count;
  }

  template <typename T, typename... Args>
    requires valid_query_each_args_v<T, Args...>
  std::enable_if_t<iguana::non_ylt_refletable_v<T>, uint64_t> query_each(
      const std::string &sql, Args &&...args) {
    reset_error();
    static_assert(iguana::is_tuple<T>::value);
    check_query_each_args<T, Args...>();
    constexpr auto SIZE = std::tuple_size_v<T>;
    constexpr auto RESULT_SIZE = result_size<T>::value;
#ifdef ORMPP_ENABLE_LOG
    std::cout << sql << std::endl;
#endif
    stmt_ = mysql_stmt_init(con_);
    if (!stmt_) {
      set_last_error(mysql_error(con_));
      return 0;
    }

    auto guard = guard_statment(stmt_);

    if (mysql_stmt_prepare(stmt_, sql.c_str(),
                           static_cast<unsigned long>(sql.size()))) {
      set_last_error(mysql_stmt_error(stmt_));
      return 0;
    }

    auto params = std::forward_as_tuple(std::forward<Args>(args)...);
    static_assert(sizeof...(Args) >= 1,
                  "query_each requires a callback as the last argument");
    constexpr size_t param_count = sizeof...(Args) - 1;
    auto bind_params =
        decay_tuple_prefix(params, std::make_index_sequence<param_count>{});
    std::vector<MYSQL_BIND> input_binds;
    if constexpr (param_count > 0) {
      input_bind_buffers_.clear();
      for_each_tuple_prefix(
          bind_params,
          [this, &input_binds](auto &&arg) {
            set_param_bind(input_binds, arg);
          },
          std::make_index_sequence<param_count>{});
      if (mysql_stmt_bind_param(stmt_, input_binds.data())) {
        set_last_error(mysql_stmt_error(stmt_));
        return 0;
      }
    }

    if (mysql_stmt_execute(stmt_)) {
      set_last_error(mysql_stmt_error(stmt_));
      return 0;
    }

    meta_ = mysql_stmt_result_metadata(stmt_);
    if (!meta_) {
      set_last_error(mysql_stmt_error(stmt_));
      return 0;
    }

    auto meta_guard = guard_result(meta_);
    if (mysql_num_fields(meta_) != RESULT_SIZE) {
      set_last_error("query_each result column count mismatch");
      return 0;
    }

    auto &&callback = std::get<param_count>(params);
    std::array<mysql_null_flag_t, RESULT_SIZE> nulls = {};
    std::array<unsigned long, RESULT_SIZE> lengths = {};
    std::array<MYSQL_BIND, RESULT_SIZE> result_binds = {};
    std::map<size_t, std::vector<char>> mp;
    T bind_row{};
    size_t index = 0;
    ormpp::for_each(
        bind_row,
        [&result_binds, &index, &nulls, &lengths, &mp, this](auto &item,
                                                             auto /*index*/) {
          using U = ylt::reflection::remove_cvref_t<decltype(item)>;
          if constexpr (iguana::ylt_refletable_v<U>) {
            ylt::reflection::for_each(
                item, [&result_binds, &index, &nulls, &lengths, &mp, this](
                          auto &field, auto /*name*/, auto /*index*/) {
                  set_param_bind(this->meta_, result_binds[index], field, index,
                                 mp, nulls[index]);
                  result_binds[index].length = &lengths[index];
                  index++;
                });
          }
          else {
            set_param_bind(this->meta_, result_binds[index], item, index, mp,
                           nulls[index]);
            result_binds[index].length = &lengths[index];
            index++;
          }
        },
        std::make_index_sequence<SIZE>{});

    if (index == 0) {
      set_last_error("query_each result bind count is zero");
      return 0;
    }

    if (mysql_stmt_bind_result(stmt_, result_binds.data())) {
      set_last_error(mysql_stmt_error(stmt_));
      return 0;
    }

    uint64_t count = 0;
    int fetch_ret = 0;
    bool stopped = false;
    while (true) {
      fetch_ret = mysql_stmt_fetch(stmt_);
      if (fetch_ret == MYSQL_DATA_TRUNCATED) {
        if (!fetch_truncated_columns(result_binds, lengths, mp)) {
          return count;
        }
      }
      else if (fetch_ret != 0) {
        break;
      }
      T tp{};
      index = 0;
      ormpp::for_each(
          tp,
          [&result_binds, &index, &mp, &nulls, this](auto &item,
                                                     auto /*index*/) {
            using U = ylt::reflection::remove_cvref_t<decltype(item)>;
            if constexpr (iguana::ylt_refletable_v<U>) {
              ylt::reflection::for_each(
                  item, [&result_binds, &index, &mp, &nulls, this](
                            auto &field, auto /*name*/, auto /*index*/) {
                    if (nulls.at(index) != 0) {
                      using W =
                          ylt::reflection::remove_cvref_t<decltype(field)>;
                      if constexpr (std::is_default_constructible_v<W> &&
                                    std::is_assignable_v<decltype(field), W>) {
                        field = {};
                      }
                      ++index;
                      return;
                    }
                    set_value(result_binds.at(index), field, index, mp);
                    index++;
                  });
            }
            else {
              if (nulls.at(index) != 0) {
                if constexpr (std::is_default_constructible_v<U> &&
                              std::is_assignable_v<decltype(item), U>) {
                  item = {};
                }
                ++index;
                return;
              }
              set_value(result_binds.at(index), item, index, mp);
              index++;
            }
          },
          std::make_index_sequence<SIZE>{});

      ++count;
      if (!invoke_query_each_callback(callback, tp)) {
        stopped = true;
        break;
      }
    }

    if (!stopped && fetch_ret != MYSQL_NO_DATA) {
      if (fetch_ret == MYSQL_DATA_TRUNCATED) {
        set_last_error("query_each mysql_stmt_fetch data truncated");
      }
      else {
        set_last_error(mysql_stmt_error(stmt_));
      }
    }

    return count;
  }

  template <typename... Args>
  auto select(Args... args) {
    return ormpp::select(this, args...);
  }

  auto select_all() { return ormpp::select_all(this); }

  template <typename T>
  auto make_update() {
    return ormpp::make_update_builder<T>(this);
  }

  template <typename T>
  auto make_delete() {
    return ormpp::make_delete_builder<T>(this);
  }

  template <typename T>
  auto make_create_table() {
    return ormpp::make_create_table_builder<T>(this);
  }

  template <typename T>
  auto make_alter_table() {
    return ormpp::make_alter_table_builder<T>(this);
  }

  // if there is a sql error, how to tell the user? throw exception?
  template <typename T, typename... Args>
  std::enable_if_t<iguana::ylt_refletable_v<T>, std::vector<T>> query(
      Args &&...args) {
    constexpr auto SIZE = ylt::reflection::members_count_v<T>;
    std::string sql = generate_query_sql<T>(db_type_v, args...);
#ifdef ORMPP_ENABLE_LOG
    std::cout << sql << std::endl;
#endif

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

    meta_ = mysql_stmt_result_metadata(stmt_);
    if (!meta_) {
      set_last_error(mysql_stmt_error(stmt_));
      return {};
    }

    auto meta_guard = guard_result(meta_);

    std::array<mysql_null_flag_t, SIZE> nulls = {};
    std::array<unsigned long, SIZE> lengths = {};
    std::array<MYSQL_BIND, SIZE> param_binds = {};
    std::map<size_t, std::vector<char>> mp;

    T t{};
    size_t index = 0;
    std::vector<T> v;
    ylt::reflection::for_each(
        t, [&param_binds, &index, &nulls, &lengths, &mp, this](
               auto &field, auto /*name*/, auto /*index*/) {
          set_param_bind(this->meta_, param_binds[index], field, index, mp,
                         nulls[index]);
          param_binds[index].length = &lengths[index];
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

    int fetch_ret3 = 0;
    while ((fetch_ret3 = mysql_stmt_fetch(stmt_)) == 0 ||
           fetch_ret3 == MYSQL_DATA_TRUNCATED) {
      ylt::reflection::for_each(
          t, [&param_binds, &mp, this](auto &field, auto /*name*/, auto index) {
            set_value(param_binds.at(index), field, index, mp);
          });

      for (auto &p : mp) {
        p.second.assign(p.second.size(), 0);
      }

      ylt::reflection::for_each(t, [nulls](auto &field, auto /*name*/,
                                           auto index) {
        if (nulls.at(index)) {
          using U = std::remove_reference_t<decltype(field)>;
          if constexpr (is_optional_v<U>::value || std::is_arithmetic_v<U>) {
            field = {};
          }
        }
      });

      v.push_back(std::move(t));
    }

    return v;
  }

  // for tuple and string with args...
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

    meta_ = mysql_stmt_result_metadata(stmt_);
    if (!meta_) {
      set_last_error(mysql_stmt_error(stmt_));
      return {};
    }

    auto meta_guard = guard_result(meta_);

    std::array<mysql_null_flag_t, result_size<T>::value> nulls = {};
    std::array<unsigned long, result_size<T>::value> lengths = {};
    std::array<MYSQL_BIND, result_size<T>::value> param_binds = {};
    std::map<size_t, std::vector<char>> mp;

    T tp{};
    size_t index = 0;
    std::vector<T> v;
    ormpp::for_each(
        tp,
        [&param_binds, &index, &nulls, &lengths, &mp, this](auto &item,
                                                            auto /*index*/) {
          using U = ylt::reflection::remove_cvref_t<decltype(item)>;
          if constexpr (iguana::ylt_refletable_v<U>) {
            ylt::reflection::for_each(
                item, [&param_binds, &index, &nulls, &lengths, &mp, this](
                          auto &field, auto /*name*/, auto /*index*/) {
                  set_param_bind(this->meta_, param_binds[index], field, index,
                                 mp, nulls[index]);
                  param_binds[index].length = &lengths[index];
                  index++;
                });
          }
          else {
            set_param_bind(this->meta_, param_binds[index], item, index, mp,
                           nulls[index]);
            param_binds[index].length = &lengths[index];
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

    int fetch_ret2 = 0;
    while ((fetch_ret2 = mysql_stmt_fetch(stmt_)) == 0 ||
           fetch_ret2 == MYSQL_DATA_TRUNCATED) {
      index = 0;
      ormpp::for_each(
          tp,
          [&param_binds, &index, &mp, this](auto &item, auto /*index*/) {
            using U = ylt::reflection::remove_cvref_t<decltype(item)>;
            if constexpr (iguana::ylt_refletable_v<U>) {
              ylt::reflection::for_each(
                  item, [&param_binds, &index, &mp, this](
                            auto &field, auto /*name*/, auto /*index*/) {
                    set_value(param_binds.at(index), field, index, mp);
                    index++;
                  });
            }
            else {
              set_value(param_binds.at(index), item, index, mp);
              index++;
            }
          },
          std::make_index_sequence<SIZE>{});

      for (auto &p : mp) {
        p.second.assign(p.second.size(), 0);
      }

      index = 0;
      ormpp::for_each(
          tp,
          [&index, nulls](auto &item, auto /*index*/) {
            using U = ylt::reflection::remove_cvref_t<decltype(item)>;
            if constexpr (iguana::ylt_refletable_v<U>) {
              ylt::reflection::for_each(
                  item,
                  [&index, nulls](auto &field, auto /*name*/, auto /*index*/) {
                    if (nulls.at(index++)) {
                      using W = std::remove_reference_t<decltype(field)>;
                      if constexpr (is_optional_v<W>::value ||
                                    std::is_arithmetic_v<W>) {
                        field = {};
                      }
                    }
                  });
            }
            else {
              if (nulls.at(index++)) {
                if constexpr (is_optional_v<U>::value ||
                              std::is_arithmetic_v<U>) {
                  item = {};
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
    last_affect_rows_ = (int)mysql_stmt_affected_rows(stmt_);
    return true;
  }

  // transaction
  void set_enable_transaction(bool enable) { transaction_ = enable; }

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
    std::set<std::string> not_null;
    std::set<std::string> unique;
    std::set<std::string> auto_primary_key;
    std::set<std::string> primary_keys;

    std::string_view auto_key = get_auto_key<T>();
    if (!auto_key.empty()) {
      auto_primary_key.insert(std::string(auto_key));
    }

    // 宏定义的conflict keys作为联合主键，优先级比ormpp_key更高
    auto pks = get_conflict_keys<T>(db_type_v);
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

    auto table_name = get_short_struct_name<T>();
    const auto type_name_arr = get_type_names<T>(DBType::mysql);

    std::string sql;
    sql.append("CREATE TABLE IF NOT EXISTS ").append(table_name).append("(");
    T t;
    ylt::reflection::for_each(t, [&](auto &field, auto name, size_t index) {
      using item_type = std::decay_t<decltype(field)>;
      sql.append(name).append(" ").append(type_name_arr[index]);

      std::string str_name(name);

      if (!auto_primary_key.empty() &&
          auto_primary_key.find(str_name) != auto_primary_key.end()) {
        sql.append(" AUTO_INCREMENT ");
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

  void append_sql_param_bind(std::vector<MYSQL_BIND> &param_binds,
                             const sql_param_value &value) {
    std::visit(
        [&](const auto &item) {
          using U = std::decay_t<decltype(item)>;
          if constexpr (std::is_same_v<U, std::nullptr_t>) {
            MYSQL_BIND param = {};
            param.buffer_type = MYSQL_TYPE_NULL;
            param_binds.push_back(param);
          }
          else {
            set_param_bind(param_binds, item);
          }
        },
        value);
  }

  template <typename T>
  void bind_update_arg(std::vector<MYSQL_BIND> &param_binds, const T &,
                       const update_where_condition &where) {
    for (const auto &param : where.condition.params) {
      append_sql_param_bind(param_binds, param);
    }
  }

  template <typename T, auto... keys>
  void bind_update_arg(std::vector<MYSQL_BIND> &param_binds, const T &t,
                       update_by_fields<keys...>) {
    (set_param_bind(param_binds,
                    ylt::reflection::get<ylt::reflection::index_of<keys>()>(t)),
     ...);
  }

  template <typename T, typename Arg>
  void bind_update_arg(std::vector<MYSQL_BIND> &, const T &, Arg &&) {}

  template <auto... members, typename T, typename... Args>
  int stmt_execute(const T &t, OptType type, Args &&...args) {
    std::vector<MYSQL_BIND> param_binds;
    input_bind_buffers_.clear();
    constexpr auto arr = indexs_of<members...>();
    if constexpr (sizeof...(members) > 0) {
      (set_param_bind(
           param_binds,
           ylt::reflection::get<ylt::reflection::index_of<members>()>(t)),
       ...);
    }
    else {
      ylt::reflection::for_each(t, [arr, &param_binds, type, this](
                                       auto &field, auto name, auto index) {
        if (type == OptType::insert && is_auto_key<T>(name)) {
          return;
        }
        if constexpr (sizeof...(members) > 0) {
          for (auto idx : arr) {
            if (idx == index) {
              set_param_bind(param_binds, field);
            }
          }
        }
        else {
          set_param_bind(param_binds, field);
        }
      });
    }

    if constexpr (sizeof...(Args) > 0) {
      if (type == OptType::update) {
        (bind_update_arg(param_binds, t, args), ...);
      }
    }
    else {
      if (type == OptType::update) {
        ylt::reflection::for_each(
            t, [&param_binds, this](auto &field, auto name, auto /*index*/) {
              std::string field_name = "`";
              field_name += name;
              field_name += "`";
              if (is_conflict_key<T>(field_name, db_type_v)) {
                set_param_bind(param_binds, field);
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

    uint64_t count = (uint64_t)mysql_stmt_affected_rows(stmt_);
    if (count == 0) {
      return type == OptType::update ? count : INT_MIN;
    }

    return count;
  }

  template <typename T, typename... Args>
  int insert_impl(OptType type, const T &t, Args &&...args) {
    auto res = insert_or_update_impl(
        t, generate_insert_sql<T>(db_type_v, type == OptType::insert), type);
    return res.has_value() ? res.value() : INT_MIN;
  }

  template <typename T, typename... Args>
  int insert_impl(OptType type, const std::vector<T> &v, Args &&...args) {
    auto res = insert_or_update_impl(
        v, generate_insert_sql<T>(db_type_v, type == OptType::insert), type);
    return res.has_value() ? res.value() : INT_MIN;
  }

  template <auto... members, typename T, typename... Args>
  int update_impl(const T &t, Args &&...args) {
    auto sql = generate_update_sql<T, members...>(db_type_v,
                                                  std::forward<Args>(args)...);
    if (sql.empty()) {
      set_last_error("update requires a conflict key or where condition");
      return INT_MIN;
    }
    auto res = insert_or_update_impl<members...>(t, sql, OptType::update, false,
                                                 std::forward<Args>(args)...);
    return res.has_value() ? res.value() : INT_MIN;
  }

  template <auto... members, typename T, typename... Args>
  int update_impl(const std::vector<T> &v, Args &&...args) {
    auto sql = generate_update_sql<T, members...>(db_type_v,
                                                  std::forward<Args>(args)...);
    if (sql.empty()) {
      set_last_error("update requires a conflict key or where condition");
      return INT_MIN;
    }
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

    if (transaction_ && !get_insert_id && !begin()) {
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
    }

    if (transaction_ && !get_insert_id && !commit()) {
      return std::nullopt;
    }

    return get_insert_id ? stmt_->mysql->insert_id : (int)v.size();
  }

 private:
  struct guard_statment {
    guard_statment(MYSQL_STMT *stmt) : stmt_(stmt) { reset_error(); }
    ~guard_statment() {
      if (stmt_ != nullptr) {
        auto status = mysql_stmt_close(stmt_);
        if (status) {
          set_last_error("close statment error code " + std::to_string(status));
        }
      }
    }

   private:
    MYSQL_STMT *stmt_ = nullptr;
  };

  struct guard_result {
    guard_result(MYSQL_RES *res) : res_(res) {}
    ~guard_result() {
      if (res_) {
        mysql_free_result(res_);
      }
    }

   private:
    MYSQL_RES *res_ = nullptr;
  };

 private:
  MYSQL *con_ = nullptr;
  MYSQL_STMT *stmt_ = nullptr;
  MYSQL_RES *meta_ = nullptr;
  int last_affect_rows_ = 0;
  std::list<std::vector<char>> input_bind_buffers_;
  inline static std::string sv_;
  inline static std::string last_error_;
  inline static bool has_error_ = false;
  inline static bool transaction_ = true;
};
}  // namespace ormpp

#endif  // ORM_MYSQL_HPP
