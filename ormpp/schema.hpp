#pragma once

#include <array>
#include <cstddef>
#include <string_view>
#include <type_traits>

#include "entity.hpp"
#include "iguana/ylt/reflection/member_names.hpp"

namespace ormpp {

struct column_schema_annotations {
  bool primary_key = false;
  bool auto_increment = false;
  bool not_null = false;
  bool unique = false;
  bool foreign_key = false;
  std::string_view referenced_table;
  std::string_view referenced_column;
};

#ifdef YLT_USE_CXX26_REFLECTION
namespace schema_detail {

template <typename T>
struct reference_annotation : std::false_type {};

template <std::meta::info Target>
struct reference_annotation<references<Target>> : std::true_type {
  static constexpr std::meta::info target = Target;
};

consteval std::string_view short_type_name(std::string_view name) {
  const auto pos = name.rfind(':');
  return pos == std::string_view::npos ? name : name.substr(pos + 1);
}

template <std::meta::info Member>
consteval column_schema_annotations parse_column_annotations() {
  column_schema_annotations result{};
  static constexpr auto annotations =
      ylt::reflection::reflect26::annotations_array<Member>();

  template for (constexpr auto annotation : annotations) {
    using annotation_t =
        ylt::reflection::reflect26::remove_cvref_meta_type_t<annotation>;
    if constexpr (std::is_same_v<annotation_t, id_t>) {
      result.primary_key = true;
      result.auto_increment = true;
    }
    else if constexpr (std::is_same_v<annotation_t, primary_key_t>) {
      result.primary_key = true;
    }
    else if constexpr (std::is_same_v<annotation_t, auto_increment_t>) {
      result.auto_increment = true;
      result.primary_key = true;
    }
    else if constexpr (std::is_same_v<annotation_t, not_null_t>) {
      result.not_null = true;
    }
    else if constexpr (std::is_same_v<annotation_t, unique_t>) {
      result.unique = true;
    }
    else if constexpr (reference_annotation<annotation_t>::value) {
      constexpr auto target = reference_annotation<annotation_t>::target;
      using referenced_type = typename[:std::meta::parent_of(target):];
      result.foreign_key = true;
      result.referenced_table =
          short_type_name(ylt::reflection::get_struct_name<referenced_type>());
      result.referenced_column =
          ylt::reflection::reflect26::member_name<target>();
    }
  }

  return result;
}

template <typename T>
consteval auto parse_entity_annotations() {
  static constexpr auto members =
      ylt::reflection::reflect26::data_members_array<T>();
  std::array<column_schema_annotations, members.size()> result{};
  std::size_t index = 0;
  template for (constexpr auto member : members) {
    result[index++] = parse_column_annotations<member>();
  }
  return result;
}

}  // namespace schema_detail
#endif

template <typename T>
inline constexpr auto get_column_schema_annotations() {
#ifdef YLT_USE_CXX26_REFLECTION
  return schema_detail::parse_entity_annotations<
      ylt::reflection::remove_cvref_t<T>>();
#else
  return std::array<column_schema_annotations,
                    ylt::reflection::members_count_v<T>>{};
#endif
}

template <typename T>
inline constexpr std::string_view get_annotated_auto_key() {
  constexpr auto annotations = get_column_schema_annotations<T>();
  constexpr auto names = ylt::reflection::get_member_names<T>();
  for (std::size_t i = 0; i < annotations.size(); ++i) {
    if (annotations[i].auto_increment) {
      return names[i];
    }
  }
  return {};
}

}  // namespace ormpp
