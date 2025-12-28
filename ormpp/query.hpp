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
  std::string sort_order;

  col_info& desc() {
    sort_order = " DESC ";
    return *this;
  }

  col_info& asc() {
    sort_order = " ASC ";
    return *this;
  }

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
  std::string name(field.class_name);
  name.append(".").append(field.name);
  if constexpr (std::is_arithmetic_v<value_type>) {
    return where_condition{name, op, std::to_string(val)};
  }
  else {
    return where_condition{name, op, val, true};
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

template <typename T>
inline std::string join_impl(std::string prefix, auto field1, auto field2) {
  std::string sql;
  sql.append(prefix).append("join ");
  if (ylt::reflection::get_struct_name<T>() == field1.class_name) {
    sql.append(field2.class_name);
  }
  else {
    sql.append(field1.class_name);
  }

  sql.append(" ON ")
      .append(field1.class_name)
      .append(".")
      .append(field1.name)
      .append("=")
      .append(field2.class_name)
      .append(".")
      .append(field2.name)
      .append(" ");
  return sql;
}

template <typename DB, typename R>
struct stage_select;

template <typename DB>
struct stage_aggregate_t;

template <typename T, typename DB, typename R>
class query_builder {
  struct context {
    DB db_;
    std::string sql_;
    std::string select_clause_;
    std::string from_clause_;
    std::string join_clause_;
    std::string where_clause_;
    std::string order_by_clause_;
    std::string desc_clause_;
    std::string limit_clause_;
    std::string offset_clause_;
    std::string count_clause_;
    std::string sum_clause_;
    std::string avg_clause_;
    std::string min_clause_;
    std::string max_clause_;

    std::string aggregate_clause() {
      if (!count_clause_.empty()) {
        return count_clause_;
      }

      if (!sum_clause_.empty()) {
        return sum_clause_;
      }

      if (!avg_clause_.empty()) {
        return avg_clause_;
      }

      if (!min_clause_.empty()) {
        return min_clause_;
      }

      if (!max_clause_.empty()) {
        return max_clause_;
      }
      return "";
    }

    auto collect() {
      if (!select_clause_.empty()) {
        sql_.append("select ").append(select_clause_).append(from_clause_);
        if (!where_clause_.empty()) {
          where_clause_.insert(0, " where ");
        }
      }

      sql_.append(join_clause_)
          .append(where_clause_)
          .append(order_by_clause_)
          .append(desc_clause_)
          .append(limit_clause_)
          .append(offset_clause_);

      if constexpr (std::is_integral_v<R>) {
        std::string sql = "select ";
        sql.append(aggregate_clause())
            .append(" from ")
            .append(ylt::reflection::get_struct_name<T>())
            .append(";");
        auto t = db_->template query_s<std::tuple<R>>(sql);
        if (t.empty()) {
          return R{};
        }
        return std::get<0>(t.front());
      }
      else {
        if constexpr (std::is_void_v<R>) {
          return db_->template query_s<T>(sql_);
        }
        else {
          auto t = db_->template query_s<R>(sql_);
          return t;
        }
      }
    }
  };

 public:
  query_builder(DB db) : db_(db), ctx_(std::make_shared<context>()) {
    ctx_->db_ = db;
  }

  auto collect() { return ctx_->collect(); }

  struct stage_offset {
    std::shared_ptr<context> ctx;

    auto collect() { return ctx->collect(); }
  };

  struct stage_limit {
    std::shared_ptr<context> ctx;

    stage_offset offset(uint64_t row) {
      ctx->offset_clause_.append(" offset ").append(std::to_string(row));
      return stage_offset{ctx};
    }

    auto collect() { return ctx->collect(); }
  };

  struct stage_order {
    std::shared_ptr<context> ctx;

    stage_limit limit(uint64_t n) {
      ctx->limit_clause_ = " LIMIT " + std::to_string(n);
      return stage_limit{ctx};
    }

    auto collect() { return ctx->collect(); }
  };

  struct stage_where {
    std::shared_ptr<context> ctx;

    template <typename... Args>
    stage_order order_by(Args... fields) {
      ctx->order_by_clause_ = " ORDER BY ";
      (ctx->order_by_clause_.append(fields.name)
           .append(fields.sort_order)
           .append(","),
       ...);
      ctx->order_by_clause_.pop_back();
      return stage_order{ctx};
    }

    stage_limit limit(uint64_t n) {
      ctx->limit_c = " LIMIT " + std::to_string(n);
      return stage_limit{ctx};
    }

    auto collect() { return ctx->collect(); }
  };

  stage_where where(const where_condition& condition) {
    ctx_->where_clause_ = condition.to_sql();
    return stage_where{ctx_};
  }

  struct stage_inner_join {
    std::shared_ptr<context> ctx;

    stage_inner_join& inner_join(auto field1, auto field2) {
      std::string sql = join_impl<T>(" inner ", field1, field2);
      ctx->join_clause_.append(sql);
      return *this;
    }

    stage_where where(const where_condition& condition) {
      ctx->where_clause_ = condition.to_sql();
      return stage_where{ctx};
    }

    auto collect() { return ctx->collect(); }
  };

  auto inner_join(auto field1, auto field2) {
    std::string sql = join_impl<T>(" inner ", field1, field2);
    ctx_->join_clause_ = sql;
    return stage_inner_join{ctx_};
  }

  auto left_join(auto field1, auto field2) {
    std::string sql = join_impl<T>(" left ", field1, field2);
    ctx_->join_clause_ = sql;
    return stage_inner_join{ctx_};
  }

  auto right_join(auto field1, auto field2) {
    std::string sql = join_impl<T>(" right ", field1, field2);
    ctx_->join_clause_ = sql;
    return stage_inner_join{ctx_};
  }

  auto full_join(auto field1, auto field2) {
    std::string sql = join_impl<T>(" full ", field1, field2);
    ctx_->join_clause_ = sql;
    return stage_inner_join{ctx_};
  }

  auto full_outer_join(auto field1, auto field2) {
    std::string sql = join_impl<T>(" full outer ", field1, field2);
    ctx_->join_clause_ = sql;
    return stage_inner_join{ctx_};
  }

  struct stage_from {
    std::shared_ptr<context> ctx;

    stage_where where(const where_condition& condition) {
      ctx->where_clause_ = condition.to_sql();
      return stage_where{ctx};
    }

    template <typename U = T>
    auto collect() {
      return ctx->template collect();
    }
  };

 private:
  friend struct stage_select<DB, R>;
  friend struct stage_aggregate_t<DB>;
  DB db_;
  std::shared_ptr<context> ctx_;
};

template <typename DB, typename R = void>
struct stage_select {
  DB db_;
  std::string select_clause_;

  template <typename T>
  query_builder<T, DB, R> from() {
    auto builder = query_builder<T, DB, R>{db_};
    if (!select_clause_.empty()) {
      builder.ctx_->select_clause_ = select_clause_;
      builder.ctx_->from_clause_.append(" from ").append(
          ylt::reflection::get_struct_name<T>());
    }
    return builder;
  }
};

template <typename T>
concept HasName = requires(T t) { t.name; };

template <typename DB, typename... Args>
stage_select<DB, std::tuple<typename Args::value_type...>> select(
    DB db, Args... args) {
  static_assert(sizeof...(Args) > 0, "must choose at least one field");
  using Tuple = std::tuple<typename Args::value_type...>;
  stage_select<DB, Tuple> sel{db};

  (sel.select_clause_.append(args.class_name)
       .append(".")
       .append(args.name)
       .append(","),
   ...);
  sel.select_clause_.pop_back();

  return sel;
}

template <typename DB>
stage_select<DB, void> select_all(DB db) {
  return stage_select<DB>{db};
}

enum class aggregate_type { COUNT, SUM, AVG, MIN, MAX };

template <typename DB>
struct stage_aggregate_t {
  DB db_;
  std::string aggregate_clause_;
  aggregate_type type_;

  template <typename T>
  query_builder<T, DB, uint64_t> from() {
    auto builder = query_builder<T, DB, uint64_t>{db_};
    set_clause(builder);
    return builder;
  }

 private:
  void set_clause(auto& builder) {
    switch (type_) {
      case aggregate_type::COUNT:
        builder.ctx_->count_clause_ = aggregate_clause_;
        break;
      case aggregate_type::SUM:
        builder.ctx_->sum_clause_ = aggregate_clause_;
        break;
      case aggregate_type::AVG:
        builder.ctx_->avg_clause_ = aggregate_clause_;
        break;
      case aggregate_type::MIN:
        builder.ctx_->min_clause_ = aggregate_clause_;
        break;
      case aggregate_type::MAX:
        builder.ctx_->max_clause_ = aggregate_clause_;
        break;
    }
  }
};

template <typename DB>
auto select_count(DB db) {
  // include null
  return stage_aggregate_t<DB>{db, "COUNT(*)", aggregate_type::COUNT};
}

template <typename DB>
auto build_aggregate(DB db, std::string clause, const auto& field,
                     aggregate_type type) {
  std::string str;
  str.append(clause).append(field.name).append(") ");
  return stage_aggregate_t<DB>{db, str, aggregate_type::COUNT};
}

template <typename DB>
auto select_count(DB db, const auto& field) {
  // exclude null
  return build_aggregate(db, " COUNT(", field, aggregate_type::COUNT);
}

template <typename DB>
auto select_count_distinct(DB db, const auto& field) {
  // distinct count, exclude null
  return build_aggregate(db, " COUNT(DISTINCT ", field, aggregate_type::COUNT);
}

template <typename DB>
auto select_sum(DB db, const auto& field) {
  return build_aggregate(db, " SUM(", field, aggregate_type::SUM);
}

template <typename DB>
auto select_avg(DB db, const auto& field) {
  return build_aggregate(db, " AVG(", field, aggregate_type::AVG);
}

template <typename DB>
auto select_min(DB db, const auto& field) {
  return build_aggregate(db, " MIN(", field, aggregate_type::MIN);
}

template <typename DB>
auto select_max(DB db, const auto& field) {
  return build_aggregate(db, " MAX(", field, aggregate_type::MAX);
}
}  // namespace ormpp