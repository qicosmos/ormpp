//
// Created by qiyu on 10/20/17.
//
#ifndef ORM_ENTITY_HPP
#define ORM_ENTITY_HPP
#include <set>
#include <string>

#include "iguana/ylt/reflection/reflect26_compat.hpp"

#ifdef YLT_USE_CXX26_REFLECTION
#include <meta>
#endif

struct ormpp_not_null {
  std::set<std::string> fields;
};

struct ormpp_key {
  std::string fields;
};

struct ormpp_auto_key {
  std::string fields;
};

struct ormpp_unique {
  std::set<std::string> fields;
};

namespace ormpp {

// C++26 reflection annotations. Expose constexpr values so the common case is
// concise: [[=ormpp::id]] std::int64_t id{};
struct id_t {};
struct primary_key_t {};
struct auto_increment_t {};
struct not_null_t {};
struct unique_t {};

inline constexpr id_t id{};
inline constexpr primary_key_t primary_key{};
inline constexpr auto_increment_t auto_increment{};
inline constexpr not_null_t not_null{};
inline constexpr unique_t unique{};

#ifdef YLT_USE_CXX26_REFLECTION
template <std::meta::info Target>
struct references {
  static constexpr std::meta::info target = Target;
};
#endif

}  // namespace ormpp

#endif  // ORM_ENTITY_HPP
