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

template <typename M>
struct col_ref {
  using value_type = M;

  std::string_view name;
  std::string_view class_name;

  where_condition like(std::string str) {
    std::string like_cond = "`";
    like_cond.append(str).append("`");
    return where_condition{std::string(name), " like ", like_cond};
  }
};

#define col(c)                                                 \
  col_ref<typename ylt::reflection::internal::member_tratis<   \
      decltype(c)>::value_type> {                              \
    ylt::reflection::field_string<c>(),                        \
        ylt::reflection::get_struct_name<                      \
            typename ylt::reflection::internal::member_tratis< \
                decltype(c)>::owner_type>()                    \
  }

#define name(c) std::string(col(c).name)

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

template <typename M>
auto operator==(col_ref<M> field, M val) {
  return where_condition{std::string(field.name), "=", std::to_string(val)};
}

template <typename M>
auto operator>=(col_ref<M> field, M val) {
  return where_condition{std::string(field.name), ">=", std::to_string(val)};
}

template <typename M>
auto operator<=(col_ref<M> field, M val) {
  return where_condition{std::string(field.name), "<=", std::to_string(val)};
}

template <typename M>
auto operator!=(col_ref<M> field, M val) {
  return where_condition{std::string(field.name), "!=", std::to_string(val)};
}

template <typename M>
auto operator>(col_ref<M> field, M val) {
  return where_condition{std::string(field.name), ">", std::to_string(val)};
}

template <typename M>
auto operator<(col_ref<M> field, M val) {
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
    sql.append(left.class_name)
        .append(".")
        .append(left.name)
        .append("=")
        .append(right.class_name)
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