//
// Created by qiyu on 10/20/17.
//

#ifndef ORM_MYSQL_HPP
#define ORM_MYSQL_HPP

#include <algorithm>
#include <atomic>
#include <climits>
#include <cstdint>
#include <cstring>
#include <deque>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "entity.hpp"
#include "query.hpp"
#include "type_mapping.hpp"

namespace ormpp {

class mysql {
 public:
  static constexpr DBType db_type_v = DBType::mysql;

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

  // Values above this limit are clamped by set_max_mysql_result_buffer_size().
  static constexpr unsigned long mysql_result_buffer_size_hard_limit =
      1024UL * 1024UL * 1024UL;

  static void set_max_mysql_result_buffer_size(unsigned long size) {
    if (size > mysql_result_buffer_size_hard_limit) {
      size = mysql_result_buffer_size_hard_limit;
    }
    max_mysql_result_buffer_size_.store(size, std::memory_order_relaxed);
  }

  static unsigned long get_max_mysql_result_buffer_size() {
    return max_mysql_result_buffer_size_.load(std::memory_order_relaxed);
  }

 private:
  using mysql_null_type =
      std::remove_pointer_t<decltype(std::declval<MYSQL_BIND>().is_null)>;
  using mysql_error_type =
      std::remove_pointer_t<decltype(std::declval<MYSQL_BIND>().error)>;
  using mysql_text_param_storage = std::vector<std::unique_ptr<std::string>>;
  using mysql_blob_param_storage = std::vector<std::unique_ptr<blob>>;
  using mysql_param_length_storage =
      std::vector<std::unique_ptr<unsigned long>>;
  using mysql_param_null_storage =
      std::vector<std::unique_ptr<mysql_null_type>>;
  using mysql_column_buffer_storage = std::vector<std::vector<char>>;
  inline static std::atomic<unsigned long> max_mysql_result_buffer_size_{
      64UL * 1024UL * 1024UL};

  static unsigned long mysql_buffer_length(std::size_t size) {
    if (size > (std::numeric_limits<unsigned long>::max)()) {
      throw std::length_error("mysql buffer length exceeds unsigned long");
    }

    return static_cast<unsigned long>(size);
  }

  static unsigned int mysql_column_index(size_t index) {
    if (index > (std::numeric_limits<unsigned int>::max)()) {
      throw std::length_error("mysql column index exceeds unsigned int");
    }

    return static_cast<unsigned int>(index);
  }

  static std::size_t bounded_c_string_length(const char *data,
                                             std::size_t max_size) {
    std::size_t len = 0;
    while (len < max_size && data[len] != '\0') {
      ++len;
    }

    return len;
  }

  template <typename Optional>
  static decltype(auto) mysql_optional_value(Optional &&value) {
    if constexpr (std::is_lvalue_reference_v<Optional &&>) {
      return *value;
    }
    else {
      return *std::forward<Optional>(value);
    }
  }

  std::optional<std::vector<char>> fetch_column_data(
      size_t column, enum_field_types buffer_type, unsigned long length) {
    auto max_buffer_size =
        max_mysql_result_buffer_size_.load(std::memory_order_relaxed);
    if (length > max_buffer_size) {
      set_last_error("mysql result column length " + std::to_string(length) +
                     " exceeds max buffer size " +
                     std::to_string(max_buffer_size));
      return std::nullopt;
    }

    if (length > (std::numeric_limits<std::size_t>::max)() - 1) {
      set_last_error("mysql result column length exceeds max buffer size");
      return std::nullopt;
    }

    auto buffer_size = static_cast<std::size_t>(length) + 1;
    std::vector<char> buffer;
    try {
      buffer = std::vector<char>(buffer_size, 0);
    } catch (const std::exception &e) {
      set_last_error("mysql result buffer allocation failed: " +
                     std::string(e.what()));
      return std::nullopt;
    }
    unsigned long fetched_length = 0;
    MYSQL_BIND param = {};
    param.buffer_type = buffer_type;
    param.buffer = buffer.data();
    param.buffer_length = mysql_buffer_length(buffer.size());
    param.length = &fetched_length;

    auto retcode =
        mysql_stmt_fetch_column(stmt_, &param, mysql_column_index(column), 0);
    if (retcode != 0) {
      set_last_error(mysql_stmt_error(stmt_));
      return std::nullopt;
    }

    if (fetched_length > length) {
      set_last_error("mysql fetched column length exceeds requested length");
      return std::nullopt;
    }

    buffer.resize(fetched_length);
    return buffer;
  }

  std::optional<std::string> fetch_column_text(size_t column,
                                               enum_field_types buffer_type,
                                               unsigned long length) {
    auto buffer = fetch_column_data(column, buffer_type, length);
    if (!buffer) {
      return std::nullopt;
    }
    if (buffer->empty()) {
      return std::string{};
    }

    return std::string(buffer->data(), buffer->data() + buffer->size());
  }

  std::optional<std::string> get_column_text_value(
      MYSQL_BIND &param_bind, size_t i, mysql_column_buffer_storage &mp) {
    if (!param_bind.length) {
      set_last_error("mysql result length is not available");
      return std::nullopt;
    }

    auto len = *param_bind.length;
    if (param_bind.is_null && *param_bind.is_null) {
      set_last_error("mysql result column is NULL");
      return std::nullopt;
    }
    if ((param_bind.error && *param_bind.error) ||
        len > param_bind.buffer_length) {
      return fetch_column_text(i, param_bind.buffer_type, len);
    }

    auto &vec = mp[i];
    return std::string(vec.data(), vec.data() + len);
  }

  static void bind_text_param(MYSQL_BIND &param, const char *data,
                              std::size_t size,
                              mysql_text_param_storage &storage,
                              mysql_param_length_storage &length_storage) {
    // Text-like parameters are copied so MYSQL_BIND never points at a temporary
    // wrapper/string_view owned by the caller.
    auto stored = size == 0 ? std::make_unique<std::string>()
                            : std::make_unique<std::string>(data, data + size);
    auto buffer_length = mysql_buffer_length(stored->size());
    if (stored->empty()) {
      // Keep a per-parameter backing byte even when length is zero, so MySQL
      // never receives a nullptr for a non-NULL text bind.
      stored->resize(1);
    }
    param.buffer_type = MYSQL_TYPE_STRING;
    param.buffer = static_cast<void *>(stored->data());
    param.buffer_length = buffer_length;
    auto length = std::make_unique<unsigned long>(buffer_length);
    param.length = length.get();
    storage.push_back(std::move(stored));
    length_storage.push_back(std::move(length));
  }

  static void bind_blob_param(MYSQL_BIND &param, const blob &value,
                              mysql_blob_param_storage &storage,
                              mysql_param_length_storage &length_storage) {
    // Blob parameters are copied for stable storage and an explicit length.
    auto stored = std::make_unique<blob>(value);
    auto buffer_length = mysql_buffer_length(stored->size());
    if (stored->empty()) {
      // Keep a per-parameter backing byte even when length is zero, so MySQL
      // never receives a nullptr for a non-NULL blob bind.
      stored->resize(1);
    }
    param.buffer_type = MYSQL_TYPE_BLOB;
    param.buffer = static_cast<void *>(stored->data());
    param.buffer_length = buffer_length;
    auto length = std::make_unique<unsigned long>(buffer_length);
    param.length = length.get();
    storage.push_back(std::move(stored));
    length_storage.push_back(std::move(length));
  }

  static void bind_null_param(MYSQL_BIND &param,
                              mysql_param_null_storage &null_storage) {
    param.buffer_type = MYSQL_TYPE_NULL;
    auto is_null = std::make_unique<mysql_null_type>(true);
    param.is_null = is_null.get();
    null_storage.push_back(std::move(is_null));
  }

  template <typename T>
  void set_param_bind(std::vector<MYSQL_BIND> &param_binds, T &&value,
                      mysql_text_param_storage &text_storage,
                      mysql_blob_param_storage &blob_storage,
                      mysql_param_length_storage &length_storage,
                      mysql_param_null_storage &null_storage) {
    MYSQL_BIND param = {};
    using U = ylt::reflection::remove_cvref_t<T>;
    if constexpr (is_optional_v<U>::value) {
      if (value.has_value()) {
        auto &&item = mysql_optional_value(std::forward<T>(value));
        return set_param_bind(param_binds, std::forward<decltype(item)>(item),
                              text_storage, blob_storage, length_storage,
                              null_storage);
      }
      else {
        bind_null_param(param, null_storage);
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
      bind_text_param(param, value.data(), value.size(), text_storage,
                      length_storage);
    }
    else if constexpr (is_db_text_type_v<U>) {
      bind_text_param(param, value.data(), value.size(), text_storage,
                      length_storage);
    }
    else if constexpr (iguana::array_v<U>) {
      using value_type = typename U::value_type;
      if constexpr (std::is_same_v<std::remove_cv_t<value_type>, char>) {
        bind_text_param(param, value.data(),
                        bounded_c_string_length(value.data(), value.size()),
                        text_storage, length_storage);
      }
      else {
        static_assert(!sizeof(U), "only char arrays are supported as strings");
      }
    }
    else if constexpr (iguana::c_array_v<U>) {
      using value_type = std::remove_extent_t<U>;
      if constexpr (std::is_same_v<std::remove_cv_t<value_type>, char>) {
        bind_text_param(param, value,
                        bounded_c_string_length(value, std::extent_v<U>),
                        text_storage, length_storage);
      }
      else {
        static_assert(!sizeof(U), "only char arrays are supported as strings");
      }
    }
    else if constexpr (std::is_same_v<const char *, U> ||
                       std::is_same_v<char *, U>) {
      if (value == nullptr) {
        bind_null_param(param, null_storage);
      }
      else {
        bind_text_param(param, value, strlen(value), text_storage,
                        length_storage);
      }
    }
    else if constexpr (std::is_same_v<blob, U>) {
      bind_blob_param(param, value, blob_storage, length_storage);
    }
#ifdef ORMPP_WITH_CSTRING
    else if constexpr (std::is_same_v<CString, U>) {
      bind_text_param(param, value.GetString(), value.GetLength(), text_storage,
                      length_storage);
    }
#endif
    else {
      static_assert(!sizeof(U), "this type has not supported yet");
    }
    param_binds.push_back(param);
  }

  template <typename T, typename B, typename E>
  void set_param_bind(MYSQL_RES *meta_, MYSQL_BIND &param_bind, T &&value,
                      size_t i, mysql_column_buffer_storage &mp, B &is_null,
                      unsigned long *length, E *error) {
    using U = ylt::reflection::remove_cvref_t<T>;

    if constexpr (is_optional_v<U>::value) {
      using value_type = typename U::value_type;
      if (!value.has_value()) {
        value = value_type{};
      }
      return set_param_bind(meta_, param_bind, *value, i, mp, is_null, length,
                            error);
    }
    else if constexpr (std::is_enum_v<U>) {
      using underlying_type = std::underlying_type_t<U>;
      using buffer_type =
          std::conditional_t<std::is_unsigned_v<underlying_type>, std::uint32_t,
                             std::int32_t>;
      param_bind.buffer_type = MYSQL_TYPE_LONG;
      param_bind.is_unsigned = std::is_unsigned_v<buffer_type>;
      mp[i].assign(sizeof(buffer_type), 0);
      param_bind.buffer = mp[i].data();
      param_bind.buffer_length = mysql_buffer_length(mp[i].size());
      param_bind.length = length;
      param_bind.error = error;
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
      mp[i].assign(sizeof(U), 0);
      param_bind.buffer = mp[i].data();
      param_bind.buffer_length = mysql_buffer_length(mp[i].size());
      param_bind.length = length;
      param_bind.error = error;
    }
    else if constexpr (std::is_same_v<std::string, U> ||
                       std::is_same_v<std::string_view, U>) {
      unsigned long buffer_size = 256;
      enum_field_types buffer_type = MYSQL_TYPE_STRING;

      MYSQL_FIELD *field =
          mysql_fetch_field_direct(meta_, mysql_column_index(i));
      if (field) {
        if (field->type == MYSQL_TYPE_MEDIUM_BLOB ||
            field->type == MYSQL_TYPE_LONG_BLOB) {
          buffer_type = field->type;
        }
      }

      param_bind.buffer_type = buffer_type;
      std::vector<char> tmp(buffer_size, 0);
      mp[i] = std::move(tmp);
      param_bind.buffer = mp[i].data();
      param_bind.buffer_length = mysql_buffer_length(buffer_size);
      param_bind.length = length;
      param_bind.error = error;
    }
    else if constexpr (is_db_text_type_v<U>) {
      unsigned long buffer_size = 256;
      enum_field_types buffer_type = MYSQL_TYPE_STRING;

      param_bind.buffer_type = buffer_type;
      std::vector<char> tmp(buffer_size, 0);
      mp[i] = std::move(tmp);
      param_bind.buffer = mp[i].data();
      param_bind.buffer_length = mysql_buffer_length(buffer_size);
      param_bind.length = length;
      param_bind.error = error;
    }
    else if constexpr (iguana::array_v<U>) {
      param_bind.buffer_type = MYSQL_TYPE_VAR_STRING;
      std::vector<char> tmp(sizeof(U), 0);
      mp[i] = std::move(tmp);
      param_bind.buffer = mp[i].data();
      param_bind.buffer_length = mysql_buffer_length(sizeof(U));
      param_bind.length = length;
      param_bind.error = error;
    }
    else if constexpr (std::is_same_v<blob, U>) {
      unsigned long buffer_size = 65536;
      enum_field_types buffer_type = MYSQL_TYPE_BLOB;

      MYSQL_FIELD *field =
          mysql_fetch_field_direct(meta_, mysql_column_index(i));
      if (field) {
        buffer_type = field->type;
      }

      param_bind.buffer_type = buffer_type;
      std::vector<char> tmp(buffer_size, 0);
      mp[i] = std::move(tmp);
      param_bind.buffer = mp[i].data();
      param_bind.buffer_length = mysql_buffer_length(buffer_size);
      param_bind.length = length;
      param_bind.error = error;
    }
#ifdef ORMPP_WITH_CSTRING
    else if constexpr (std::is_same_v<CString, U>) {
      unsigned long buffer_size = 256;
      enum_field_types buffer_type = MYSQL_TYPE_STRING;

      MYSQL_FIELD *field =
          mysql_fetch_field_direct(meta_, mysql_column_index(i));
      if (field) {
        if (field->type == MYSQL_TYPE_MEDIUM_BLOB ||
            field->type == MYSQL_TYPE_LONG_BLOB) {
          buffer_type = field->type;
        }
      }

      param_bind.buffer_type = buffer_type;
      std::vector<char> tmp(buffer_size, 0);
      mp[i] = std::move(tmp);
      param_bind.buffer = mp[i].data();
      param_bind.buffer_length = mysql_buffer_length(buffer_size);
      param_bind.length = length;
      param_bind.error = error;
    }
#endif
    else {
      static_assert(!sizeof(U), "this type has not supported yet");
    }
    param_bind.is_null = &is_null;
  }

  template <typename T>
  bool set_value(MYSQL_BIND &param_bind, T &&value, size_t i,
                 mysql_column_buffer_storage &mp, bool is_null) {
    using U = ylt::reflection::remove_cvref_t<T>;
    if (is_null) {
      if constexpr (is_optional_v<U>::value) {
        value = std::nullopt;
      }
      else if constexpr (std::is_enum_v<U> || std::is_arithmetic_v<U> ||
                         std::is_same_v<std::string, U> ||
                         std::is_same_v<std::string_view, U> ||
                         is_db_text_type_v<U> || std::is_same_v<blob, U>) {
        value = {};
      }
      else if constexpr (iguana::array_v<U>) {
        std::memset(value.data(), 0, value.size());
      }
#ifdef ORMPP_WITH_CSTRING
      else if constexpr (std::is_same_v<CString, U>) {
        value.Empty();
      }
#endif
      return true;
    }

    if constexpr (is_optional_v<U>::value) {
      using value_type = typename U::value_type;
      if constexpr (std::is_enum_v<value_type>) {
        using underlying_type = std::underlying_type_t<value_type>;
        using buffer_type =
            std::conditional_t<std::is_unsigned_v<underlying_type>,
                               std::uint32_t, std::int32_t>;
        buffer_type item{};
        memcpy(&item, param_bind.buffer, sizeof(item));
        value = static_cast<value_type>(item);
      }
      else if constexpr (std::is_arithmetic_v<value_type>) {
        value_type item{};
        memcpy(&item, param_bind.buffer, sizeof(value_type));
        value = std::move(item);
      }
      else {
        value_type item;
        value = std::move(item);
        return set_value(param_bind, *value, i, mp, false);
      }
    }
    else if constexpr (std::is_enum_v<U>) {
      using underlying_type = std::underlying_type_t<U>;
      using buffer_type =
          std::conditional_t<std::is_unsigned_v<underlying_type>, std::uint32_t,
                             std::int32_t>;
      buffer_type item{};
      memcpy(&item, param_bind.buffer, sizeof(item));
      value = static_cast<U>(item);
    }
    else if constexpr (std::is_arithmetic_v<U>) {
      memcpy(&value, param_bind.buffer, sizeof(U));
    }
    else if constexpr (std::is_same_v<std::string, U>) {
      auto text = get_column_text_value(param_bind, i, mp);
      if (!text) {
        return false;
      }
      value = std::move(*text);
    }
    else if constexpr (std::is_same_v<std::string_view, U>) {
      auto text = get_column_text_value(param_bind, i, mp);
      if (!text) {
        return false;
      }
      string_view_storage_.push_back(std::move(*text));
      value = string_view_storage_.back();
    }
    else if constexpr (is_db_text_type_v<U>) {
      auto text = get_column_text_value(param_bind, i, mp);
      if (!text) {
        return false;
      }
      value.value = std::move(*text);
    }
    else if constexpr (iguana::array_v<U>) {
      auto &vec = mp[i];
      memcpy(value.data(), vec.data(), value.size());
    }
    else if constexpr (std::is_same_v<blob, U>) {
      auto &vec = mp[i];
      if (!param_bind.length) {
        set_last_error("mysql result length is not available");
        return false;
      }

      auto len = *param_bind.length;
      if ((param_bind.error && *param_bind.error) ||
          len > param_bind.buffer_length) {
        auto data = fetch_column_data(i, param_bind.buffer_type, len);
        if (!data) {
          return false;
        }
        value = std::move(*data);
      }
      else {
        value = blob(vec.data(), vec.data() + len);
      }
    }
#ifdef ORMPP_WITH_CSTRING
    else if constexpr (std::is_same_v<CString, U>) {
      auto text = get_column_text_value(param_bind, i, mp);
      if (!text) {
        return false;
      }
      value.SetString(text->c_str());
    }
#endif
    return true;
  }

 public:
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

    mysql_text_param_storage text_param_storage;
    mysql_blob_param_storage blob_param_storage;
    mysql_param_length_storage param_length_storage;
    mysql_param_null_storage param_null_storage;
    std::vector<MYSQL_BIND> sql_param_binds;
    if constexpr (sizeof...(Args) > 0) {
      try {
        (set_param_bind(sql_param_binds, args, text_param_storage,
                        blob_param_storage, param_length_storage,
                        param_null_storage),
         ...);
      } catch (const std::exception &e) {
        set_last_error(e.what());
        return 0;
      }
      if (mysql_stmt_bind_param(stmt_, &sql_param_binds[0])) {
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
    string_view_storage_.clear();
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

    mysql_text_param_storage text_param_storage;
    mysql_blob_param_storage blob_param_storage;
    mysql_param_length_storage param_length_storage;
    mysql_param_null_storage param_null_storage;
    std::vector<MYSQL_BIND> sql_param_binds;
    if constexpr (sizeof...(Args) > 0) {
      try {
        (set_param_bind(sql_param_binds, args, text_param_storage,
                        blob_param_storage, param_length_storage,
                        param_null_storage),
         ...);
      } catch (const std::exception &e) {
        set_last_error(e.what());
        return {};
      }
      if (mysql_stmt_bind_param(stmt_, &sql_param_binds[0])) {
        set_last_error(mysql_stmt_error(stmt_));
        return {};
      }
    }

    std::array<mysql_null_type, SIZE> nulls = {};
    std::array<unsigned long, SIZE> lengths = {};
    std::array<mysql_error_type, SIZE> errors = {};
    std::array<MYSQL_BIND, SIZE> param_binds = {};
    mysql_column_buffer_storage mp(SIZE);

    T t{};
    size_t index = 0;
    std::vector<T> v;
    ylt::reflection::for_each(
        t, [&param_binds, &index, &nulls, &lengths, &errors, &mp, this](
               auto &field, auto /*name*/, auto /*index*/) {
          set_param_bind(this->meta_, param_binds[index], field, index, mp,
                         nulls[index], &lengths[index], &errors[index]);
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
      bool row_ok = true;
      ylt::reflection::for_each(t, [&param_binds, &nulls, &mp, &row_ok, this](
                                       auto &field, auto /*name*/, auto index) {
        if (!row_ok) {
          return;
        }
        row_ok =
            set_value(param_binds.at(index), field, index, mp, nulls.at(index));
      });
      if (!row_ok) {
        return {};
      }

      for (auto &buffer : mp) {
        std::fill(buffer.begin(), buffer.end(), 0);
      }

      v.push_back(std::move(t));
    }

    return v;
  }

  template <typename T, typename... Args>
  std::enable_if_t<iguana::non_ylt_refletable_v<T>, std::vector<T>> query_s(
      const std::string &sql, Args &&...args) {
    string_view_storage_.clear();
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

    mysql_text_param_storage text_param_storage;
    mysql_blob_param_storage blob_param_storage;
    mysql_param_length_storage param_length_storage;
    mysql_param_null_storage param_null_storage;
    std::vector<MYSQL_BIND> sql_param_binds;
    if constexpr (sizeof...(Args) > 0) {
      try {
        (set_param_bind(sql_param_binds, args, text_param_storage,
                        blob_param_storage, param_length_storage,
                        param_null_storage),
         ...);
      } catch (const std::exception &e) {
        set_last_error(e.what());
        return {};
      }
      if (mysql_stmt_bind_param(stmt_, &sql_param_binds[0])) {
        set_last_error(mysql_stmt_error(stmt_));
        return {};
      }
    }

    std::array<mysql_null_type, result_size<T>::value> nulls = {};
    std::array<unsigned long, result_size<T>::value> lengths = {};
    std::array<mysql_error_type, result_size<T>::value> errors = {};
    std::array<MYSQL_BIND, result_size<T>::value> param_binds = {};
    mysql_column_buffer_storage mp(result_size<T>::value);

    T tp{};
    size_t index = 0;
    std::vector<T> v;
    ormpp::for_each(
        tp,
        [&param_binds, &index, &nulls, &lengths, &errors, &mp, this](
            auto &item, auto /*index*/) {
          using U = ylt::reflection::remove_cvref_t<decltype(item)>;
          if constexpr (iguana::ylt_refletable_v<U>) {
            ylt::reflection::for_each(
                item, [&param_binds, &index, &nulls, &lengths, &errors, &mp,
                       this](auto &field, auto /*name*/, auto /*index*/) {
                  set_param_bind(this->meta_, param_binds[index], field, index,
                                 mp, nulls[index], &lengths[index],
                                 &errors[index]);
                  index++;
                });
          }
          else {
            set_param_bind(this->meta_, param_binds[index], item, index, mp,
                           nulls[index], &lengths[index], &errors[index]);
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
      bool row_ok = true;
      ormpp::for_each(
          tp,
          [&param_binds, &index, &nulls, &mp, &row_ok, this](auto &item,
                                                             auto /*index*/) {
            if (!row_ok) {
              return;
            }
            using U = ylt::reflection::remove_cvref_t<decltype(item)>;
            if constexpr (iguana::ylt_refletable_v<U>) {
              ylt::reflection::for_each(
                  item, [&param_binds, &index, &nulls, &mp, &row_ok, this](
                            auto &field, auto /*name*/, auto /*index*/) {
                    if (!row_ok) {
                      return;
                    }
                    row_ok = set_value(param_binds.at(index), field, index, mp,
                                       nulls.at(index));
                    index++;
                  });
            }
            else {
              row_ok = set_value(param_binds.at(index), item, index, mp,
                                 nulls.at(index));
              index++;
            }
          },
          std::make_index_sequence<SIZE>{});
      if (!row_ok) {
        return {};
      }

      for (auto &buffer : mp) {
        std::fill(buffer.begin(), buffer.end(), 0);
      }

      v.push_back(std::move(tp));
    }

    return v;
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
    string_view_storage_.clear();
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

    std::array<mysql_null_type, SIZE> nulls = {};
    std::array<unsigned long, SIZE> lengths = {};
    std::array<mysql_error_type, SIZE> errors = {};
    std::array<MYSQL_BIND, SIZE> param_binds = {};
    mysql_column_buffer_storage mp(SIZE);

    T t{};
    size_t index = 0;
    std::vector<T> v;
    ylt::reflection::for_each(
        t, [&param_binds, &index, &nulls, &lengths, &errors, &mp, this](
               auto &field, auto /*name*/, auto /*index*/) {
          set_param_bind(this->meta_, param_binds[index], field, index, mp,
                         nulls[index], &lengths[index], &errors[index]);
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
      bool row_ok = true;
      ylt::reflection::for_each(t, [&param_binds, &nulls, &mp, &row_ok, this](
                                       auto &field, auto /*name*/, auto index) {
        if (!row_ok) {
          return;
        }
        row_ok =
            set_value(param_binds.at(index), field, index, mp, nulls.at(index));
      });
      if (!row_ok) {
        return {};
      }

      for (auto &buffer : mp) {
        std::fill(buffer.begin(), buffer.end(), 0);
      }

      v.push_back(std::move(t));
    }

    return v;
  }

  // for tuple and string with args...
  template <typename T, typename Arg, typename... Args>
  std::enable_if_t<iguana::non_ylt_refletable_v<T>, std::vector<T>> query(
      const Arg &s, Args &&...args) {
    string_view_storage_.clear();
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

    std::array<mysql_null_type, result_size<T>::value> nulls = {};
    std::array<unsigned long, result_size<T>::value> lengths = {};
    std::array<mysql_error_type, result_size<T>::value> errors = {};
    std::array<MYSQL_BIND, result_size<T>::value> param_binds = {};
    mysql_column_buffer_storage mp(result_size<T>::value);

    T tp{};
    size_t index = 0;
    std::vector<T> v;
    ormpp::for_each(
        tp,
        [&param_binds, &index, &nulls, &lengths, &errors, &mp, this](
            auto &item, auto /*index*/) {
          using U = ylt::reflection::remove_cvref_t<decltype(item)>;
          if constexpr (iguana::ylt_refletable_v<U>) {
            ylt::reflection::for_each(
                item, [&param_binds, &index, &nulls, &lengths, &errors, &mp,
                       this](auto &field, auto /*name*/, auto /*index*/) {
                  set_param_bind(this->meta_, param_binds[index], field, index,
                                 mp, nulls[index], &lengths[index],
                                 &errors[index]);
                  index++;
                });
          }
          else {
            set_param_bind(this->meta_, param_binds[index], item, index, mp,
                           nulls[index], &lengths[index], &errors[index]);
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
      bool row_ok = true;
      ormpp::for_each(
          tp,
          [&param_binds, &index, &nulls, &mp, &row_ok, this](auto &item,
                                                             auto /*index*/) {
            if (!row_ok) {
              return;
            }
            using U = ylt::reflection::remove_cvref_t<decltype(item)>;
            if constexpr (iguana::ylt_refletable_v<U>) {
              ylt::reflection::for_each(
                  item, [&param_binds, &index, &nulls, &mp, &row_ok, this](
                            auto &field, auto /*name*/, auto /*index*/) {
                    if (!row_ok) {
                      return;
                    }
                    row_ok = set_value(param_binds.at(index), field, index, mp,
                                       nulls.at(index));
                    index++;
                  });
            }
            else {
              row_ok = set_value(param_binds.at(index), item, index, mp,
                                 nulls.at(index));
              index++;
            }
          },
          std::make_index_sequence<SIZE>{});
      if (!row_ok) {
        return {};
      }

      for (auto &buffer : mp) {
        std::fill(buffer.begin(), buffer.end(), 0);
      }

      v.push_back(std::move(tp));
    }

    return v;
  }

  unsigned long get_blob_len(size_t column) {
    reset_error();
    unsigned long data_len = 0;

    MYSQL_BIND param;
    memset(&param, 0, sizeof(MYSQL_BIND));
    param.length = &data_len;
    param.buffer_type = MYSQL_TYPE_BLOB;

    auto retcode =
        mysql_stmt_fetch_column(stmt_, &param, mysql_column_index(column), 0);
    if (retcode != 0) {
      set_last_error(mysql_stmt_error(stmt_));
      return 0;
    }

    return data_len;
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
              str.append(quote_mysql_identifier(name)).append(",");
            }
            str.pop_back();
            unique.insert(str);
          }
          else {
            unique.insert(quote_mysql_identifier(*item.fields.begin()));
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
      sql.append(quote_mysql_identifier(name))
          .append(" ")
          .append(type_name_arr[index]);

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
      sql.append("PRIMARY KEY (")
          .append(quote_mysql_identifier(auto_key))
          .append("),");
    }
    else if (!primary_keys.empty()) {
      sql.append("PRIMARY KEY (");
      for (auto key : primary_keys) {
        sql.append(quote_mysql_identifier(key)).append(",");
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

  template <auto... members, typename T, typename... Args>
  int stmt_execute(const T &t, OptType type, Args &&...args) {
    std::vector<MYSQL_BIND> param_binds;
    mysql_text_param_storage text_param_storage;
    mysql_blob_param_storage blob_param_storage;
    mysql_param_length_storage param_length_storage;
    mysql_param_null_storage param_null_storage;
    constexpr auto arr = indexs_of<members...>();
    try {
      if constexpr (sizeof...(members) > 0) {
        (set_param_bind(
             param_binds,
             ylt::reflection::get<ylt::reflection::index_of<members>()>(t),
             text_param_storage, blob_param_storage, param_length_storage,
             param_null_storage),
         ...);
      }
      else {
        ylt::reflection::for_each(
            t, [arr, &param_binds, &text_param_storage, &blob_param_storage,
                type, &param_length_storage, &param_null_storage,
                this](auto &field, auto name, auto index) {
              if (type == OptType::insert && is_insert_skipped<T>(name)) {
                return;
              }
              if constexpr (sizeof...(members) > 0) {
                for (auto idx : arr) {
                  if (idx == index) {
                    set_param_bind(param_binds, field, text_param_storage,
                                   blob_param_storage, param_length_storage,
                                   param_null_storage);
                  }
                }
              }
              else {
                set_param_bind(param_binds, field, text_param_storage,
                               blob_param_storage, param_length_storage,
                               param_null_storage);
              }
            });
      }

      if constexpr (sizeof...(Args) == 0) {
        if (type == OptType::update) {
          ylt::reflection::for_each(
              t, [&param_binds, &text_param_storage, &blob_param_storage,
                  &param_length_storage, &param_null_storage,
                  this](auto &field, auto name, auto /*index*/) {
                std::string field_name = "`";
                field_name += name;
                field_name += "`";
                if (is_conflict_key<T>(field_name, db_type_v)) {
                  set_param_bind(param_binds, field, text_param_storage,
                                 blob_param_storage, param_length_storage,
                                 param_null_storage);
                }
              });
        }
      }
    } catch (const std::exception &e) {
      set_last_error(e.what());
      return INT_MIN;
    }

    if (!param_binds.empty() &&
        mysql_stmt_bind_param(stmt_, param_binds.data())) {
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
  std::deque<std::string> string_view_storage_;
  inline static std::string last_error_;
  inline static bool has_error_ = false;
  inline static bool transaction_ = true;
};
}  // namespace ormpp

#endif  // ORM_MYSQL_HPP
