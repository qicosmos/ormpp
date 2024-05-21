//
// Created by qiyu on 11/3/17.
//

#ifndef ORM_DBNG_HPP
#define ORM_DBNG_HPP

#include <chrono>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "utility.hpp"

namespace ormpp {
template <typename DB>
class dbng {
 public:
  dbng() = default;
  dbng(const dbng &) = delete;
  ~dbng() { disconnect(); }

  template <typename... Args>
  bool connect(Args &&...args) {
    return db_.connect(std::forward<Args>(args)...);
  }

  bool disconnect() { return db_.disconnect(); }

  template <typename T, typename... Args>
  bool create_datatable(Args &&...args) {
    return db_.template create_datatable<T>(std::forward<Args>(args)...);
  }

  template <typename T, typename... Args>
  int insert(const T &t, Args &&...args) {
    return db_.insert(t, std::forward<Args>(args)...);
  }

  template <typename T, typename... Args>
  int insert(const std::vector<T> &v, Args &&...args) {
    return db_.insert(v, std::forward<Args>(args)...);
  }

  template <typename T, typename... Args>
  int replace(const T &t, Args &&...args) {
    return db_.replace(t, std::forward<Args>(args)...);
  }

  template <typename T, typename... Args>
  int replace(const std::vector<T> &v, Args &&...args) {
    return db_.replace(v, std::forward<Args>(args)...);
  }

  template <typename T, typename... Args>
  int update(const T &t, Args &&...args) {
    return db_.update(t, std::forward<Args>(args)...);
  }

  template <typename T, typename... Args>
  int update(const std::vector<T> &v, Args &&...args) {
    return db_.update(v, std::forward<Args>(args)...);
  }

  template <auto... members, typename T, typename... Args>
  int update_some(const T &t, Args &&...args) {
    return db_.template update<members...>(t, std::forward<Args>(args)...);
  }

  template <auto... members, typename T, typename... Args>
  int update_some(const std::vector<T> &v, Args &&...args) {
    return db_.template update<members...>(v, std::forward<Args>(args)...);
  }

  template <typename T, typename... Args>
  uint64_t get_insert_id_after_insert(const T &t, Args &&...args) {
    return db_.get_insert_id_after_insert(t, std::forward<Args>(args)...);
  }

  template <typename T, typename... Args>
  uint64_t get_insert_id_after_insert(const std::vector<T> &v, Args &&...args) {
    return db_.get_insert_id_after_insert(v, std::forward<Args>(args)...);
  }

  template <typename T, typename... Args>
  bool delete_records_s(const std::string &str = "", Args &&...args) {
    return db_.template delete_records_s<T>(str, std::forward<Args>(args)...);
  }

  template <typename T, typename... Args>
  std::vector<T> query_s(const std::string &str = "", Args &&...args) {
    return db_.template query_s<T>(str, std::forward<Args>(args)...);
  }

  template <typename T, typename... Args>
  [[deprecated]] bool delete_records(Args &&...where_condition) {
    return db_.template delete_records<T>(
        std::forward<Args>(where_condition)...);
  }

  // restriction, all the args are string, the first is the where condition,
  // rest are append conditions
  template <typename T, typename... Args>
  [[deprecated]] std::vector<T> query(Args &&...args) {
    return db_.template query<T>(std::forward<Args>(args)...);
  }

  // support member variable, such as: query(FID(simple::id), "<", 5)
  template <typename Pair, typename U>
  [[deprecated]] auto query(Pair pair, std::string_view oper, U &&val) {
    auto sql = build_condition(pair, oper, std::forward<U>(val));
    using T = typename ormpp::field_attribute<decltype(pair.second)>::type;
    return query<T>(sql);
  }

  template <typename Pair, typename U>
  [[deprecated]] bool delete_records(Pair pair, std::string_view oper,
                                     U &&val) {
    auto sql = build_condition(pair, oper, std::forward<U>(val));
    using T = typename ormpp::field_attribute<decltype(pair.second)>::type;
    return delete_records<T>(sql);
  }

  bool execute(const std::string &sql) { return db_.execute(sql); }

  // transaction
  bool begin() { return db_.begin(); }

  bool commit() { return db_.commit(); }

  bool rollback() { return db_.rollback(); }

  bool ping() { return db_.ping(); }

  bool has_error() { return db_.has_error(); }

  std::string get_last_error() const { return db_.get_last_error(); }

  int get_last_affect_rows() { return db_.get_last_affect_rows(); }

 private:
  template <typename Pair, typename U>
  auto build_condition(Pair pair, std::string_view oper, U &&val) {
    std::string sql = "";
    using V = std::remove_const_t<std::remove_reference_t<U>>;

    // if field type is numeric, return type of val is numeric, to string; val
    // is string, no change; if field type is string, return type of val is
    // numeric, to string and add ''; val is string, add '';
    using return_type =
        typename field_attribute<decltype(pair.second)>::return_type;

    if constexpr (std::is_arithmetic_v<return_type> &&
                  std::is_arithmetic_v<V>) {
      append(sql, pair.first, oper, std::to_string(std::forward<U>(val)));
    }
    else if constexpr (!std::is_arithmetic_v<return_type>) {
      if constexpr (std::is_arithmetic_v<V>)
        append(sql, pair.first, oper,
               to_str(std::to_string(std::forward<U>(val))));
      else
        append(sql, pair.first, oper, to_str(std::forward<U>(val)));
    }
    else {
      append(sql, pair.first, oper, std::forward<U>(val));
    }

    return sql;
  }

#define HAS_MEMBER(member)                                               \
  template <typename T, typename... Args>                                \
  struct has_##member {                                                  \
   private:                                                              \
    template <typename U>                                                \
    static auto Check(int)                                               \
        -> decltype(std::declval<U>().member(std::declval<Args>()...),   \
                    std::true_type());                                   \
    template <typename U>                                                \
    static std::false_type Check(...);                                   \
                                                                         \
   public:                                                               \
    enum {                                                               \
      value = std::is_same<decltype(Check<T>(0)), std::true_type>::value \
    };                                                                   \
  };

  HAS_MEMBER(before)
  HAS_MEMBER(after)

#define WRAPER(func)                                                   \
  template <typename... AP, typename... Args>                          \
  auto wraper##_##func(Args &&...args) {                               \
    using result_type = decltype(std::declval<decltype(this)>()->func( \
        std::declval<Args>()...));                                     \
    bool r = true;                                                     \
    std::tuple<AP...> tp{};                                            \
    for_each_l(                                                        \
        tp,                                                            \
        [&r, &args...](auto &item) {                                   \
          if (!r)                                                      \
            return;                                                    \
          if constexpr (has_before<decltype(item)>::value)             \
            r = item.before(std::forward<Args>(args)...);              \
        },                                                             \
        std::make_index_sequence<sizeof...(AP)>{});                    \
    if (!r)                                                            \
      return result_type{};                                            \
    auto lambda = [this, &args...] {                                   \
      return this->func(std::forward<Args>(args)...);                  \
    };                                                                 \
    result_type result = std::invoke(lambda);                          \
    for_each_r(                                                        \
        tp,                                                            \
        [&r, &result, &args...](auto &item) {                          \
          if (!r)                                                      \
            return;                                                    \
          if constexpr (has_after<decltype(item), result_type>::value) \
            r = item.after(result, std::forward<Args>(args)...);       \
        },                                                             \
        std::make_index_sequence<sizeof...(AP)>{});                    \
    return result;                                                     \
  }

  template <typename... Args, typename F, std::size_t... Idx>
  constexpr void for_each_l(std::tuple<Args...> &t, F &&f,
                            std::index_sequence<Idx...>) {
    (std::forward<F>(f)(std::get<Idx>(t)), ...);
  }

  template <typename... Args, typename F, std::size_t... Idx>
  constexpr void for_each_r(std::tuple<Args...> &t, F &&f,
                            std::index_sequence<Idx...>) {
    constexpr auto size = sizeof...(Idx);
    (std::forward<F>(f)(std::get<size - Idx - 1>(t)), ...);
  }

 public:
  WRAPER(connect);
  WRAPER(execute);
  void update_operate_time() { latest_tm_ = std::chrono::system_clock::now(); }

  auto get_latest_operate_time() { return latest_tm_; }

 private:
  DB db_;
  std::chrono::system_clock::time_point latest_tm_ =
      std::chrono::system_clock::now();
};
}  // namespace ormpp

#endif  // ORM_DBNG_HPP
