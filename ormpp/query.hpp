#pragma once
#include <string>

#include "utility.hpp"

namespace ormpp {
struct where_condition {
  std::string left;
  std::string op;
  std::string right;
  bool need_quote = false;

  std::string to_sql() const {
    std::string sql;
    sql.append("(").append(left).append(op);
    if (need_quote) {
      sql.append("'");
    }
    sql.append(right);
    if (need_quote) {
      sql.append("'");
    }
    sql.append(")");
    return sql;
  }
};

template <typename M>
struct col_info {
  using value_type = M;

  std::string_view name;
  std::string_view class_name;

  template <typename... Args>
  where_condition in(Args... args) {
    return in_impl("", args...);
  }

  template <typename... Args>
  where_condition not_in(Args... args) {
    return in_impl("not", args...);
  }

  where_condition null() {
    return where_condition{std::string(name), " IS ", "NULL"};
  }

  where_condition not_null() {
    return where_condition{std::string(name), " IS ", "NOT NULL"};
  }

  template <typename T>
  where_condition between(T left, T right) {
    std::string str_left;
    str_left.append(name).append(" between ").append(to_string(left));

    std::string str_right;
    str_right.append(to_string(right));
    return where_condition{str_left, " and ", str_right};
  }

  where_condition like(std::string str) {
    return where_condition{std::string(name), " like ", to_string(str)};
  }

 private:
  template <typename value_type>
  std::string to_string(value_type val) {
    static_assert(std::is_constructible_v<M, value_type>, "invalid type");
    if constexpr (std::is_arithmetic_v<value_type>) {
      return std::to_string(val);
    }
    else {
      std::string str = "'";
      str.append(val).append("'");
      return str;
    }
  }

  template <typename... Args>
  where_condition in_impl(std::string s, Args... args) {
    std::string mid;
    (mid.append(to_string(args)).append(","), ...);
    mid.pop_back();

    std::string left;
    left.append(name).append(" ").append(s).append(" in(");
    return where_condition{left, mid, ")"};
  }
};

#define col(c)                                                 \
  col_info<typename ylt::reflection::internal::member_tratis<  \
      decltype(c)>::value_type> {                              \
    ylt::reflection::field_string<c>(),                        \
        ylt::reflection::get_struct_name<                      \
            typename ylt::reflection::internal::member_tratis< \
                decltype(c)>::owner_type>()                    \
  }

template <auto field>
constexpr std::string_view name() {
  return ylt::reflection::field_string<field>();
}

template <auto field>
std::string str_name() {
  return std::string(name<field>());
}

#define col_name(c) str_name<c>()

where_condition operator||(where_condition lhs, where_condition rhs) {
  return where_condition{lhs.to_sql(), " OR ", rhs.to_sql()};
}

where_condition operator&&(where_condition lhs, where_condition rhs) {
  return where_condition{lhs.to_sql(), " AND ", rhs.to_sql()};
}

template <typename M, typename value_type>
where_condition build_where(col_info<M> field, value_type val, std::string op) {
  static_assert(std::is_constructible_v<M, value_type>, "invalid type");
  if constexpr (std::is_arithmetic_v<value_type>) {
    return where_condition{std::string(field.name), op, std::to_string(val)};
  }
  else {
    return where_condition{std::string(field.name), op, val, true};
  }
}

template <typename M>
auto operator==(col_info<M> field, auto val) {
  return build_where(field, val, "=");
}

template <typename M>
auto operator>=(col_info<M> field, M val) {
  return build_where(field, val, ">=");
}

template <typename M>
auto operator<=(col_info<M> field, M val) {
  return build_where(field, val, "<=");
}

template <typename M>
auto operator!=(col_info<M> field, M val) {
  return build_where(field, val, "!=");
}

template <typename M>
auto operator>(col_info<M> field, M val) {
  return build_where(field, val, ">");
}

template <typename M>
auto operator<(col_info<M> field, M val) {
  return build_where(field, val, "<");
}

template <typename T, typename DB>
class query_builder {
  struct context {
    DB db_;
    std::string sql_;
    std::string where_clause_;
    std::string order_by_clause_;
    std::string desc_clause_;
    std::string limit_clause_;
    std::string offset_clause_;
    std::string count_clause_;

    template <typename U = T>
    auto collect() {
      sql_.append(where_clause_)
          .append(order_by_clause_)
          .append(desc_clause_)
          .append(limit_clause_)
          .append(offset_clause_);

      if constexpr (std::is_integral_v<U>) {
        std::string sql = "select ";
        sql.append(count_clause_)
            .append(" from ")
            .append(ylt::reflection::get_struct_name<T>())
            .append(";");
        auto t = db_->template query_s<std::tuple<U>>(sql);
        if (t.empty()) {
          return U{};
        }
        return std::get<0>(t.front());
      }
      else {
        auto t = db_->template query_s<T>(sql_);
        return t;
      }
    }
  };

 public:
  query_builder(DB db) : db_(db), ctx_(std::make_shared<context>()) {
    ctx_->db_ = db;
  }

  template <typename U = T>
  auto collect() {
    return ctx_->template collect<U>();
  }

  struct stage_offset {
    std::shared_ptr<context> ctx;

    template <typename U = T>
    auto collect() {
      return ctx->template collect<U>();
    }
  };

  struct stage_limit {
    std::shared_ptr<context> ctx;

    stage_offset offset(uint64_t row) {
      ctx->offset_clause_.append(" offset ").append(std::to_string(row));
      return stage_offset{ctx};
    }

    template <typename U = T>
    auto collect() {
      return ctx->template collect<U>();
    }
  };

  struct stage_desc {
    std::shared_ptr<context> ctx;

    stage_limit limit(uint64_t count) {
      ctx->limit_clause_.append(" limit ").append(std::to_string(count));
      return stage_limit{ctx};
    }

    template <typename U = T>
    auto collect() {
      return ctx->template collect<U>();
    }
  };

  struct stage_order {
    std::shared_ptr<context> ctx;

    stage_desc desc() {
      ctx->desc_clause_ = " DESC ";
      return stage_desc{ctx};
    }

    stage_limit limit(uint64_t n) {
      ctx->limit_clause_ = " LIMIT " + std::to_string(n);
      return stage_limit{ctx};
    }

    template <typename U = T>
    auto collect() {
      return ctx->template collect<U>();
    }
  };

  struct stage_where {
    std::shared_ptr<context> ctx;
    stage_order order_by(const auto& field) {
      ctx->order_by_clause_ = " ORDER BY " + std::string(field.name);
      return stage_order{ctx};
    }

    stage_limit limit(uint64_t n) {
      ctx->limit_c = " LIMIT " + std::to_string(n);
      return stage_limit{ctx};
    }

    template <typename U = T>
    auto collect() {
      return ctx->template collect<U>();
    }
  };

  stage_where where(const where_condition& condition) {
    ctx_->where_clause_ = condition.to_sql();
    return stage_where{ctx_};
  }

  query_builder& count() {
    ctx_->count_clause_ = "COUNT(*)";  // include null
    return *this;
  }

  query_builder& count(const auto& field) {
    ctx_->count_clause_.append(" COUNT(")
        .append(field.name)
        .append(") ");  // exclude null
    return *this;
  }

  query_builder& count_distinct(const auto& field) {
    ctx_->count_clause_.append(" COUNT(DISTINCT ")
        .append(field.name)
        .append(") ");  // distinct count, exclude null
    return *this;
  }

 private:
  DB db_;
  std::shared_ptr<context> ctx_;
};
}  // namespace ormpp