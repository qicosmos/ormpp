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

  where_condition param() {
    std::string str(class_name);
    str.append(".").append(name);
    return where_condition{str, " = ", "?  "};
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

where_condition build_where(auto field, auto val, std::string op) {
  using M = typename decltype(field)::value_type;
  using value_type = decltype(val);
  if constexpr (iguana::array_v<M>) {
    static_assert(
        std::is_same_v<typename M::value_type, typename value_type::value_type>,
        "invalid type");
  }
  else {
    static_assert(std::is_constructible_v<M, value_type>, "invalid type");
  }
  std::string name(field.class_name);
  if (name.empty()) {
    name.append(field.name);
  }
  else {
    name.append(".").append(field.name);
  }

  if constexpr (std::is_arithmetic_v<value_type>) {
    return where_condition{name, op, std::to_string(val)};
  }
  else {
    if (val == "?  ") {
      return where_condition{name, op, val};
    }
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

template <typename Arg>
struct aggregate_field {
  using value_type = Arg;
  std::string name;
  std::string_view class_name;
};

template <typename M>
auto operator==(aggregate_field<M> field, auto val) {
  return build_where(field, val, "=");
}

template <typename M>
auto operator!=(aggregate_field<M> field, auto val) {
  return build_where(field, val, "!=");
}

template <typename M>
auto operator>(aggregate_field<M> field, auto val) {
  return build_where(field, val, ">");
}

template <typename M>
auto operator>=(aggregate_field<M> field, auto val) {
  return build_where(field, val, ">=");
}

template <typename M>
auto operator<(aggregate_field<M> field, auto val) {
  return build_where(field, val, "<");
}

template <typename M>
auto operator<=(aggregate_field<M> field, auto val) {
  return build_where(field, val, "<=");
}

struct token_t {};
inline constexpr auto token = token_t{};

inline auto count() { return aggregate_field<uint64_t>{"COUNT(*)"}; }

template <typename T>
inline auto build_aggregate_field(std::string prefix, auto field) {
  std::string str = std::move(prefix);
  str.append(field.class_name).append(".").append(field.name).append(")");
  return aggregate_field<T>{str};
}

inline auto count(auto field) {
  return build_aggregate_field<uint64_t>("COUNT(", field);
}

inline auto count_distinct(auto field) {
  return build_aggregate_field<uint64_t>("COUNT(DISTINCT ", field);
}

inline auto sum(auto field) {
  return build_aggregate_field<uint64_t>("SUM(", field);
}

inline auto avg(auto field) {
  return build_aggregate_field<double>("AVG(", field);
}

template <typename Field>
inline auto(min)(Field field) {
  return build_aggregate_field<typename Field::value_type>("MIN(", field);
}

template <typename Field>
inline auto(max)(Field field) {
  return build_aggregate_field<typename Field::value_type>("MAX(", field);
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

template <typename... Args>
std::string order_by_sql(Args... fields) {
  std::string sql = " ORDER BY ";
  (sql.append(fields.name).append(fields.sort_order).append(","), ...);
  sql.pop_back();
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
    std::string group_by_clause_;
    std::string join_clause_;
    std::string where_clause_;
    std::string order_by_clause_;
    std::string having_clause_;
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

    template <typename... Args>
    auto scalar(Args... args) {
      using first = std::tuple_element_t<0, R>;
      return collect<first>(args...);
    }

    template <typename To, typename... Args>
    auto collect(Args... args) {
      if (!select_clause_.empty()) {
        sql_.append("select ").append(select_clause_).append(from_clause_);
        if (!where_clause_.empty()) {
          where_clause_.insert(0, " where ");
        }
      }

      sql_.append(join_clause_)
          .append(where_clause_)
          .append(group_by_clause_)
          .append(having_clause_)
          .append(order_by_clause_)
          .append(desc_clause_)
          .append(limit_clause_)
          .append(offset_clause_);

#ifdef ORMPP_ENABLE_PG
      if (sql_.find('?') != std::string::npos) {
        int index = 1;
        for (size_t i = 0; i < sql_.size(); i++) {
          if (sql_[i] == '?') {
            sql_[i] = '$';
            std::string index_str = std::to_string(index++);
            std::memcpy(&sql_[i + 1], index_str.data(),
                        (std::min)(index_str.size(), size_t(2)));
          }
        }
      }
#endif

      if constexpr (!ylt::reflection::is_ylt_refl_v<R> && !std::is_void_v<R> &&
                    !iguana::tuple_v<R>) {
        auto t = db_->template query_s<std::tuple<R>>(sql_, args...);
        if (t.empty()) {
          return R{};
        }
        return std::get<0>(t.front());
      }
      else {
        if constexpr (std::is_void_v<R>) {
          return db_->template query_s<T>(sql_, args...);
        }
        else {
          if constexpr (std::is_void_v<To>) {
            auto t = db_->template query_s<R>(sql_, args...);
            return t;
          }
          else {
            // To: maybe a tuple mapping struct or a first row first col type.
            if constexpr (iguana::optional_v<To> || std::is_arithmetic_v<To> ||
                          iguana::string_container_v<To>) {
              auto t = db_->template query_s<R>(sql_, args...);
              if (t.empty()) {
                return To{};
              }
              return std::get<0>(t.front());
            }
            else {
              auto t = db_->template query_s<To>(sql_, args...);
              return t;
            }
          }
        }
      }
    }

    template <typename... Args>
    auto collect(Args... args) {
      return collect<void>(args...);
    }
  };

 public:
  query_builder(DB db) : db_(db), ctx_(std::make_shared<context>()) {
    ctx_->db_ = db;
  }

  template <typename To, typename... Args>
  auto collect(Args... args) {
    return ctx_->template collect<To>(args...);
  }

  template <typename... Args>
  auto collect(Args... args) {
    return ctx_->collect(args...);
  }

  // first row and first col
  template <typename... Args>
  auto scalar(Args... args) {
    return ctx_->scalar(args...);
  }

  struct stage_offset {
    std::shared_ptr<context> ctx;

    template <typename To, typename... Args>
    auto collect(Args... args) {
      return ctx->template collect<To>(args...);
    }

    template <typename... Args>
    auto collect(Args... args) {
      return ctx->collect(args...);
    }

    template <typename... Args>
    auto scalar(Args... args) {
      return ctx->scalar(args...);
    }
  };

  struct stage_limit {
    std::shared_ptr<context> ctx;

    stage_offset offset(uint64_t row) {
      ctx->offset_clause_.append(" offset ").append(std::to_string(row));
      return stage_offset{ctx};
    }

    stage_offset offset(token_t) {
      ctx->offset_clause_.append(" offset ?  ");
      return stage_offset{ctx};
    }

    template <typename To, typename... Args>
    auto collect(Args... args) {
      return ctx->template collect<To>(args...);
    }

    template <typename... Args>
    auto collect(Args... args) {
      return ctx->collect(args...);
    }

    template <typename... Args>
    auto scalar(Args... args) {
      return ctx->scalar(args...);
    }
  };

  struct stage_order {
    std::shared_ptr<context> ctx;

    stage_limit limit(uint64_t n) {
      ctx->limit_clause_ = " LIMIT " + std::to_string(n);
      return stage_limit{ctx};
    }

    stage_limit limit(token_t) {
      ctx->limit_clause_ = " LIMIT ?  ";
      return stage_limit{ctx};
    }

    template <typename To, typename... Args>
    auto collect(Args... args) {
      return ctx->template collect<To>(args...);
    }

    template <typename... Args>
    auto collect(Args... args) {
      return ctx->collect(args...);
    }

    template <typename... Args>
    auto scalar(Args... args) {
      return ctx->scalar(args...);
    }
  };

  struct stage_having {
    std::shared_ptr<context> ctx;
    std::string cond_;

    // order by
    template <typename... Args>
    stage_order order_by(Args... fields) {
      ctx->order_by_clause_ = order_by_sql(fields...);
      return stage_order{ctx};
    }

    template <typename To, typename... Args>
    auto collect(Args... args) {
      return ctx->template collect<To>(args...);
    }

    template <typename... Args>
    auto collect(Args... args) {
      return ctx->collect(args...);
    }

    template <typename... Args>
    auto scalar(Args... args) {
      return ctx->scalar(args...);
    }
  };

  struct stage_group_by {
    std::shared_ptr<context> ctx;

    // order by
    template <typename... Args>
    stage_order order_by(Args... fields) {
      ctx->order_by_clause_ = order_by_sql(fields...);
      return stage_order{ctx};
    }

    // having
    stage_having having(auto cond) {
      ctx->having_clause_ = " HAVING ";
      ctx->having_clause_.append(cond.to_sql());
      return stage_having{ctx};
    }

    template <typename To, typename... Args>
    auto collect(Args... args) {
      return ctx->template collect<To>(args...);
    }

    template <typename... Args>
    auto collect(Args... args) {
      return ctx->collect(args...);
    }

    template <typename... Args>
    auto scalar(Args... args) {
      return ctx->scalar(args...);
    }
  };

  template <typename... Args>
  stage_group_by group_by(Args... fields) {
    ctx_->group_by_clause_ = " GROUP BY ";
    (ctx_->group_by_clause_.append(fields.name).append(","), ...);
    ctx_->group_by_clause_.pop_back();
    return stage_group_by{ctx_};
  }

  struct stage_where {
    std::shared_ptr<context> ctx;

    template <typename... Args>
    stage_order order_by(Args... fields) {
      ctx->order_by_clause_ = order_by_sql(fields...);
      return stage_order{ctx};
    }

    stage_limit limit(uint64_t n) {
      ctx->limit_c = " LIMIT " + std::to_string(n);
      return stage_limit{ctx};
    }

    stage_limit limit(token_t) {
      ctx->limit_c = " LIMIT ?  ";
      return stage_limit{ctx};
    }

    template <typename... Args>
    stage_group_by group_by(Args... fields) {
      ctx->group_by_clause_ = " GROUP BY ";
      (ctx->group_by_clause_.append(fields.class_name)
           .append(".")
           .append(fields.name)
           .append(","),
       ...);
      ctx->group_by_clause_.pop_back();
      return stage_group_by{ctx};
    }

    template <typename To, typename... Args>
    auto collect(Args... args) {
      return ctx->template collect<To>(args...);
    }

    template <typename... Args>
    auto collect(Args... args) {
      return ctx->collect(args...);
    }

    template <typename... Args>
    auto scalar(Args... args) {
      return ctx->scalar(args...);
    }
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

    template <typename To, typename... Args>
    auto collect(Args... args) {
      return ctx->template collect<To>(args...);
    }

    template <typename... Args>
    auto collect(Args... args) {
      return ctx->collect(args...);
    }

    template <typename... Args>
    auto scalar(Args... args) {
      return ctx->scalar(args...);
    }
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

 private:
  friend struct stage_select<DB, R>;
  friend struct stage_aggregate_t<DB>;
  DB db_;
  std::shared_ptr<context> ctx_;
};

template <typename DB, typename R = void>
struct stage_select {
  using ResultType = R;
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

template <typename Arg>
auto append_select(auto& sel, Arg arg) {
  if (arg.class_name.empty()) {
    sel.select_clause_.append(arg.name).append(",");
    return;
  }

  sel.select_clause_.append(arg.class_name)
      .append(".")
      .append(arg.name)
      .append(",");
}

template <typename R, typename DB, typename... Args>
auto create_stage_select(DB db, Args... args) {
  stage_select<DB, R> sel{db};
  (append_select(sel, args), ...);
  sel.select_clause_.pop_back();

  return sel;
}

template <typename DB, typename... Args>
auto select(DB db, Args... args) {
  static_assert(sizeof...(Args) > 0, "must choose at least one field");
  using Tuple = std::tuple<typename Args::value_type...>;
  using First = std::tuple_element_t<0, Tuple>;
  using ArgType = std::tuple_element_t<0, std::tuple<Args...>>;
  if constexpr (std::tuple_size_v<Tuple> == 1 &&
                std::is_same_v<ArgType, aggregate_field<First>>) {
    return create_stage_select<First>(db, args...);
  }
  else {
    return create_stage_select<Tuple>(db, args...);
  }
}

struct all_t {};

inline constexpr auto all = all_t{};

template <typename DB>
stage_select<DB, void> select_all(DB db) {
  return stage_select<DB>{db};
}

template <typename T, typename DB>
struct update_context {
  DB db_;
  std::string set_clause_;
  std::string where_clause_;

  int execute_impl() {
    std::string sql;
    sql.append("UPDATE ")
        .append(get_short_struct_name<T>())
        .append(" SET ")
        .append(set_clause_);
    if (!where_clause_.empty()) {
      sql.append(" WHERE ").append(where_clause_);
    }
#ifdef ORMPP_ENABLE_LOG
    std::cout << sql << std::endl;
#endif
    if (db_->execute(sql)) {
      return db_->get_last_affect_rows();
    }
    return -1;
  }
};

template <typename M, typename V>
void append_set(std::string& sql, col_info<M> field, V val) {
  sql.append(field.name);
  sql.append("=");
  if constexpr (std::is_arithmetic_v<V>) {
    sql.append(std::to_string(val));
  }
  else {
    sql.append("'").append(val).append("'");
  }
}

template <typename T, typename DB>
struct stage_update_where {
  std::shared_ptr<update_context<T, DB>> ctx;
  int execute() { return ctx->execute_impl(); }
};

template <typename T, typename DB>
struct stage_update_set {
  std::shared_ptr<update_context<T, DB>> ctx;
  template <typename M, typename V>
  stage_update_set& set(col_info<M> field, V val) {
    ctx->set_clause_.append(",");
    append_set(ctx->set_clause_, field, val);
    return *this;
  }

  template <typename M>
  stage_update_set& set_null(col_info<M> field) {
    ctx->set_clause_.append(",").append(field.name).append("=NULL");
    return *this;
  }

  stage_update_where<T, DB> where(const where_condition& condition) {
    ctx->where_clause_ = condition.to_sql();
    return stage_update_where<T, DB>{ctx};
  }

  int execute_all() { return ctx->execute_impl(); }
};

template <typename T, typename DB>
struct update_builder {
  DB db_;

  template <typename M, typename V>
  stage_update_set<T, DB> set(col_info<M> field, V val) {
    auto ctx = std::make_shared<update_context<T, DB>>();
    ctx->db_ = db_;
    append_set(ctx->set_clause_, field, val);
    return stage_update_set<T, DB>{ctx};
  }

  template <typename M>
  stage_update_set<T, DB> set_null(col_info<M> field) {
    auto ctx = std::make_shared<update_context<T, DB>>();
    ctx->db_ = db_;
    ctx->set_clause_.append(field.name).append("=NULL");
    return stage_update_set<T, DB>{ctx};
  }
};

template <typename T, typename DB>
update_builder<T, DB> make_update_builder(DB db) {
  return update_builder<T, DB>{db};
}

template <typename T, typename DB>
struct delete_context {
  DB db_;
  std::string where_clause_;
  int execute_impl() {
    std::string sql;
    sql.append("DELETE FROM ").append(get_short_struct_name<T>());
    if (!where_clause_.empty()) {
      sql.append(" WHERE ").append(where_clause_);
    }
#ifdef ORMPP_ENABLE_LOG
    std::cout << sql << std::endl;
#endif
    if (db_->execute(sql)) {
      return db_->get_last_affect_rows();
    }
    return -1;
  }
};

template <typename T, typename DB>
struct delete_builder {
  DB db_;
  struct stage_delete_where {
    std::shared_ptr<delete_context<T, DB>> ctx;
    int execute() { return ctx->execute_impl(); }
  };

  stage_delete_where where(const where_condition& condition) {
    auto ctx = std::make_shared<delete_context<T, DB>>();
    ctx->db_ = db_;
    ctx->where_clause_ = condition.to_sql();
    return stage_delete_where{ctx};
  }

  int execute_all() {
    auto ctx = std::make_shared<delete_context<T, DB>>();
    ctx->db_ = db_;
    return ctx->execute_impl();
  }
};

template <typename T, typename DB>
delete_builder<T, DB> make_delete_builder(DB db) {
  return delete_builder<T, DB>{db};
}

template <typename T, typename DB>
struct create_table_builder {
  static constexpr DBType db_type = std::remove_pointer_t<DB>::db_type_v;
  DB db_;
  std::string auto_increment_field_;
  std::set<std::string> primary_keys_;
  std::set<std::string> not_null_fields_;
  std::vector<std::string> unique_constraints_;
  std::map<std::string, std::string> default_values_;
  std::vector<std::string> check_constraints_;
  std::vector<std::pair<std::string, std::string>> foreign_keys_;
  std::string charset_;
  std::string engine_;

  template <typename M>
  create_table_builder& auto_increment(col_info<M> field) {
    auto_increment_field_ = std::string(field.name);
    primary_keys_.insert(auto_increment_field_);
    return *this;
  }

  template <typename... Fields>
  create_table_builder& primary_key(Fields... fields) {
    (primary_keys_.insert(std::string(fields.name)), ...);
    return *this;
  }

  template <typename... Fields>
  create_table_builder& not_null(Fields... fields) {
    (not_null_fields_.insert(std::string(fields.name)), ...);
    return *this;
  }

  template <typename... Fields>
  create_table_builder& unique(Fields... fields) {
    std::string cols;
    ((cols.empty() ? cols.append(fields.name)
                   : cols.append(",").append(fields.name)),
     ...);
    unique_constraints_.push_back(std::move(cols));
    return *this;
  }

  template <typename M1, typename M2>
  create_table_builder& foreign_key(col_info<M1> local, col_info<M2> ref) {
    std::string ref_str;
    ref_str.append(ref.class_name).append("(").append(ref.name).append(")");
    foreign_keys_.emplace_back(std::string(local.name), std::move(ref_str));
    return *this;
  }

  template <typename M, typename V>
  create_table_builder& default_value(col_info<M> field, V val) {
    if constexpr (std::is_arithmetic_v<V>) {
      default_values_[std::string(field.name)] = std::to_string(val);
    }
    else {
      std::string s = "'";
      s.append(val).append("'");
      default_values_[std::string(field.name)] = std::move(s);
    }
    return *this;
  }

  create_table_builder& check(const where_condition& cond) {
    check_constraints_.push_back(cond.to_sql());
    return *this;
  }

  create_table_builder& check(const std::string& expr) {
    check_constraints_.push_back(expr);
    return *this;
  }

  create_table_builder& charset(const std::string& cs) {
    charset_ = cs;
    return *this;
  }

  create_table_builder& engine(const std::string& eng) {
    engine_ = eng;
    return *this;
  }

  bool execute() {
    std::string sql = generate_sql();
#ifdef ORMPP_ENABLE_LOG
    std::cout << sql << std::endl;
#endif
    if (!auto_increment_field_.empty()) {
      T t{};
      ylt::reflection::for_each(
          t, [&](auto& /*field*/, auto name, size_t /*index*/) {
            if (std::string_view(name) == auto_increment_field_) {
              add_auto_key_field(get_short_struct_name<T>(), name);
            }
          });
    }
    return db_->execute(sql);
  }

 private:
  std::string generate_sql() {
    auto table_name = get_short_struct_name<T>();
    const auto type_name_arr = get_type_names<T>(db_type);
    bool auto_inline_pk = false;
    std::string sql;
    sql.append("CREATE TABLE IF NOT EXISTS ").append(table_name).append("(");
    T t{};
    ylt::reflection::for_each(t, [&](auto& /*field*/, auto name, size_t index) {
      std::string field_name(name);
      std::string type_str = type_name_arr[index];

      bool is_auto = (!auto_increment_field_.empty() &&
                      field_name == auto_increment_field_);
      if constexpr (db_type == DBType::postgresql) {
        if (is_auto) {
          if (type_str == "bigint") {
            type_str = "bigserial";
          }
          else {
            type_str = "serial";
          }
        }
      }

      sql.append(field_name).append(" ").append(type_str);
      if constexpr (db_type == DBType::sqlite) {
        if (is_auto) {
          sql.append(" PRIMARY KEY AUTOINCREMENT");
          auto_inline_pk = true;
        }
      }
      else if constexpr (db_type == DBType::mysql) {
        if (is_auto) {
          sql.append(" AUTO_INCREMENT");
        }
      }

      if (not_null_fields_.count(field_name)) {
        sql.append(" NOT NULL");
      }

      if (default_values_.count(field_name)) {
        sql.append(" DEFAULT ").append(default_values_[field_name]);
      }

      sql.append(",");
    });

    if (!auto_inline_pk && !primary_keys_.empty()) {
      sql.append("PRIMARY KEY (");
      bool first = true;
      for (auto& k : primary_keys_) {
        if (!first) {
          sql.append(",");
        }
        sql.append(k);
        first = false;
      }
      sql.append("),");
    }

    for (auto& u : unique_constraints_) {
      sql.append("UNIQUE (").append(u).append("),");
    }

    for (auto& [local, ref] : foreign_keys_) {
      sql.append("FOREIGN KEY (")
          .append(local)
          .append(") REFERENCES ")
          .append(ref)
          .append(",");
    }

    for (auto& chk : check_constraints_) {
      sql.append("CHECK (").append(chk).append("),");
    }

    if (sql.back() == ',') {
      sql.pop_back();
    }
    sql.append(")");

    if constexpr (db_type == DBType::mysql) {
      if (!engine_.empty()) {
        sql.append(" ENGINE=").append(engine_);
      }
      if (!charset_.empty()) {
        sql.append(" DEFAULT CHARSET=").append(charset_);
      }
      else {
        sql.append(" DEFAULT CHARSET=utf8mb4");
      }
    }

    return sql;
  }
};

template <typename T, typename DB>
create_table_builder<T, DB> make_create_table_builder(DB db) {
  return create_table_builder<T, DB>{db};
}

template <typename T, typename DB>
create_table_builder<T, DB> make_create_table_buillder(DB db) {
  return make_create_table_builder<T, DB>(db);
}

template <typename T, typename DB>
struct alter_table_builder {
  static constexpr DBType db_type = std::remove_pointer_t<DB>::db_type_v;
  DB db_;
  struct alter_op {
    std::string sql;
  };

  std::vector<alter_op> ops_;

  template <typename M>
  alter_table_builder& add_column(col_info<M> field,
                                  const std::string& extra = "") {
    const auto type_name_arr = get_type_names<T>(db_type);
    std::string type_str;
    size_t target_idx = 0;
    T t{};
    ylt::reflection::for_each(t, [&](auto& /*f*/, auto name, size_t index) {
      if (std::string_view(name) == field.name) {
        target_idx = index;
      }
    });

    type_str = type_name_arr[target_idx];
    std::string s;
    s.append("ADD COLUMN ").append(field.name).append(" ").append(type_str);
    if (!extra.empty()) {
      s.append(" ").append(extra);
    }
    ops_.push_back({std::move(s)});
    return *this;
  }

  alter_table_builder& add_column(const std::string& col_name,
                                  const std::string& type_and_constraints) {
    std::string s;
    s.append("ADD COLUMN ")
        .append(col_name)
        .append(" ")
        .append(type_and_constraints);
    ops_.push_back({std::move(s)});
    return *this;
  }

  template <typename M>
  alter_table_builder& drop_column(col_info<M> field) {
    std::string s;
    s.append("DROP COLUMN ").append(field.name);
    ops_.push_back({std::move(s)});
    return *this;
  }

  alter_table_builder& drop_column(const std::string& col_name) {
    std::string s;
    s.append("DROP COLUMN ").append(col_name);
    ops_.push_back({std::move(s)});
    return *this;
  }

  template <typename M>
  alter_table_builder& rename_column(col_info<M> field,
                                     const std::string& new_name) {
    std::string s;
    s.append("RENAME COLUMN ")
        .append(field.name)
        .append(" TO ")
        .append(new_name);
    ops_.push_back({std::move(s)});
    return *this;
  }

  template <typename M>
  alter_table_builder& modify_column(col_info<M> field,
                                     const std::string& new_type) {
    std::string s;
    if constexpr (db_type == DBType::mysql) {
      s.append("MODIFY COLUMN ")
          .append(field.name)
          .append(" ")
          .append(new_type);
    }
    else if constexpr (db_type == DBType::postgresql) {
      s.append("ALTER COLUMN ")
          .append(field.name)
          .append(" TYPE ")
          .append(new_type);
    }
    ops_.push_back({std::move(s)});
    return *this;
  }

  template <typename... Fields>
  alter_table_builder& add_index(const std::string& index_name,
                                 Fields... fields) {
    std::string cols;
    ((cols.empty() ? cols.append(fields.name)
                   : cols.append(",").append(fields.name)),
     ...);
    std::string s;
    if constexpr (db_type == DBType::mysql) {
      s.append("ADD INDEX ")
          .append(index_name)
          .append(" (")
          .append(cols)
          .append(")");
    }
    else {
      s.append("_CREATE__INDEX__ ").append(index_name).append(" ").append(cols);
    }
    ops_.push_back({std::move(s)});
    return *this;
  }

  alter_table_builder& drop_index(const std::string& index_name) {
    std::string s;
    if constexpr (db_type == DBType::mysql) {
      s.append("DROP INDEX ").append(index_name);
    }
    else {
      s.append("_DROP_INDEX__ ").append(index_name);
    }
    ops_.push_back({std::move(s)});
    return *this;
  }

  template <typename... Fields>
  alter_table_builder& add_unique(const std::string& constraint_name,
                                  Fields... fields) {
    std::string cols;
    ((cols.empty() ? cols.append(fields.name)
                   : cols.append(",").append(fields.name)),
     ...);
    std::string s;
    s.append("ADD CONSTRAINT ")
        .append(constraint_name)
        .append(" UNIQUE (")
        .append(cols)
        .append(")");
    ops_.push_back({std::move(s)});
    return *this;
  }

  alter_table_builder& drop_constraint(const std::string& constraint_name) {
    std::string s;
    if constexpr (db_type == DBType::mysql) {
      s.append("DROP INDEX ").append(constraint_name);
    }
    else {
      s.append("DROP CONSTRAINT ").append(constraint_name);
    }
    ops_.push_back({std::move(s)});
    return *this;
  }

  template <typename M1, typename M2>
  alter_table_builder& add_foreign_key(const std::string& constraint_name,
                                       col_info<M1> local, col_info<M2> ref) {
    std::string s;
    s.append("ADD CONSTRAINT ")
        .append(constraint_name)
        .append(" FOREIGN KEY (")
        .append(local.name)
        .append(") REFERENCES ")
        .append(ref.class_name)
        .append("(")
        .append(ref.name)
        .append(")");
    ops_.push_back({std::move(s)});
    return *this;
  }

  alter_table_builder& add_check(const std::string& constraint_name,
                                 const std::string& expr) {
    std::string s;
    s.append("ADD CONSTRAINT ")
        .append(constraint_name)
        .append(" CHECK (")
        .append(expr)
        .append(")");
    ops_.push_back({std::move(s)});
    return *this;
  }

  alter_table_builder& add_check(const std::string& constraint_name,
                                 const where_condition& cond) {
    return add_check(constraint_name, cond.to_sql());
  }

  alter_table_builder& raw(const std::string& clause) {
    ops_.push_back({clause});
    return *this;
  }

  bool execute() {
    auto table_name = get_short_struct_name<T>();
    constexpr std::string_view ci_prefix = "_CREATE__INDEX__ ";
    constexpr std::string_view di_prefix = "_DROP_INDEX__ ";
    bool ok = true;
    for (auto& op : ops_) {
      std::string sql;
      if (op.sql.compare(0, ci_prefix.size(), ci_prefix) == 0) {
        auto rest = op.sql.substr(ci_prefix.size());
        auto sp = rest.find(' ');
        auto idx_name = rest.substr(0, sp);
        auto cols = rest.substr(sp + 1);
        sql.append("CREATE INDEX ")
            .append(idx_name)
            .append(" ON ")
            .append(table_name)
            .append("(")
            .append(cols)
            .append(")");
      }
      else if (op.sql.compare(0, di_prefix.size(), di_prefix) == 0) {
        auto idx_name = op.sql.substr(di_prefix.size());
        sql.append("DROP INDEX ").append(idx_name);
      }
      else {
        sql.append("ALTER TABLE ")
            .append(table_name)
            .append(" ")
            .append(op.sql);
      }
#ifdef ORMPP_ENABLE_LOG
      std::cout << sql << std::endl;
#endif
      if (!db_->execute(sql)) {
        ok = false;
        break;
      }
    }
    return ok;
  }
};

template <typename T, typename DB>
alter_table_builder<T, DB> make_alter_table_builder(DB db) {
  return alter_table_builder<T, DB>{db};
}

template <typename T, typename DB>
alter_table_builder<T, DB> make_alter_table_buildeer(DB db) {
  return make_alter_table_builder<T, DB>(db);
}

}  // namespace ormpp
