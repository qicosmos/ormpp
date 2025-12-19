#pragma once

#include "iguana/ylt/reflection/member_names.hpp"
namespace ormpp {
// template<auto c>
// struct filed_t {
//   inline static constexpr std::string_view name =
//   ylt::reflection::field_string<c>(); using owner_type = typename
//   ylt::reflection::internal::member_tratis<
//       decltype(c)>::owner_type;
//   using value_type = typename ylt::reflection::internal::member_tratis<
//       decltype(c)>::value_type;
// };

// #define col(c) filed_t<c>{}
struct where_condition {
  std::string left;
  std::string op;
  std::string right;
  std::string sql_;

  std::string to_sql() const {
    std::string sql;
    sql.append("(").append(left).append(op).append(right).append(")");
    return sql;
  }
};

template <typename T, typename M>
struct col_ref {
  M T::*ptr;
  std::string_view name;
  using owner_type = T;
  where_condition like(std::string str) {
    std::string like_cond = "`";
    like_cond.append(str).append("`");
    return where_condition{std::string(name), " like ", like_cond};
  }
};

#define col(c)                                               \
  col_ref<typename ylt::reflection::internal::member_tratis< \
              decltype(c)>::owner_type,                      \
          typename ylt::reflection::internal::member_tratis< \
              decltype(c)>::value_type> {                    \
    c, ylt::reflection::field_string<c>()                    \
  }

template <typename T, typename M>
struct where_expr {
  T col;
  M value;
  std::string op;

  std::string to_sql() const {
    std::string sql = " where ";
    sql.append(col.name).append(op).append(std::to_string(value));
    return sql;
  }
};

where_condition operator||(where_condition lhs, where_condition rhs) {
  return where_condition{lhs.to_sql(), " OR ", rhs.to_sql()};
}

where_condition operator&&(where_condition lhs, where_condition rhs) {
  return where_condition{lhs.to_sql(), " and ", rhs.to_sql()};
}

template <typename T, typename M>
auto operator==(col_ref<T, M> field, M val) {
  return where_condition{std::string(field.name), "=", std::to_string(val)};
}

template <typename T, typename M>
auto operator>=(col_ref<T, M> field, M val) {
  return where_condition{std::string(field.name), ">=", std::to_string(val)};
}

template <typename T, typename M>
auto operator<=(col_ref<T, M> field, M val) {
  return where_condition{std::string(field.name), "<=", std::to_string(val)};
}

template <typename T, typename M>
auto operator!=(col_ref<T, M> field, M val) {
  return where_condition{std::string(field.name), "!=", std::to_string(val)};
}

template <typename T, typename M>
auto operator>(col_ref<T, M> field, M val) {
  return where_condition{std::string(field.name), ">", std::to_string(val)};
}

template <typename T, typename M>
auto operator<(col_ref<T, M> field, M val) {
  return where_condition{std::string(field.name), "<", std::to_string(val)};
}

template <typename T>
class query_builder {
 public:
  query_builder& where(where_condition condition) {
    auto cond = condition.to_sql();
    sql_.append(" where ").append(cond);
    return *this;
  }

  query_builder& inner_join(auto left, auto right) {
    using L = decltype(left);
    using R = decltype(right);
    std::string sql = " inner join on ";
    sql.append(ylt::reflection::get_struct_name<typename L::owner_type>())
        .append(".")
        .append(left.name)
        .append("=")
        .append(ylt::reflection::get_struct_name<typename R::owner_type>())
        .append(".")
        .append(right.name)
        .append(" ");

    sql_.append(sql);
    return *this;
  }

  std::string sql_ = std::string("select * from ") +
                     std::string(ylt::reflection::get_struct_name<T>());
};

template <typename T>
auto from() {
  return query_builder<T>{};
}
}  // namespace ormpp