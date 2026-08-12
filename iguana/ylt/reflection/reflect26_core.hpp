#pragma once
#ifdef YLT_USE_CXX26_REFLECTION
#include <array>
#include <cstddef>
#include <meta>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace ylt::reflection::reflect26 {

template <std::size_t N>
struct fixed_string {
  char data[N];

  consteval fixed_string(const char (&str)[N]) {
    for (std::size_t i = 0; i < N; ++i) {
      data[i] = str[i];
    }
  }

  consteval operator std::string_view() const { return {data, N - 1}; }
};

template <fixed_string Name>
struct field_name {
  static constexpr auto value = Name;
};

template <fixed_string Name>
struct struct_name {
  static constexpr auto value = Name;
};

struct skip_base {};

struct skip_field {};

inline constexpr std::string_view normalized_member_name(
    std::string_view name) {
  if (name.size() > 3 && name[0] == '_' && name[1] == '_' && name[2] == '_') {
    name.remove_prefix(3);
  }
  return name;
}

template <std::meta::info Info>
using meta_type_t = typename[:std::meta::type_of(Info):];

template <std::meta::info Info>
using remove_cvref_meta_type_t = std::remove_cvref_t<meta_type_t<Info>>;

template <std::meta::info Info>
consteval auto annotations_array() {
  return std::define_static_array(std::meta::annotations_of(Info));
}

template <typename T>
struct is_field_name_annotation : std::false_type {};

template <fixed_string Name>
struct is_field_name_annotation<field_name<Name>> : std::true_type {};

template <typename T>
struct is_struct_name_annotation : std::false_type {};

template <fixed_string Name>
struct is_struct_name_annotation<struct_name<Name>> : std::true_type {};

template <typename T>
struct is_skip_base_annotation : std::false_type {};

template <>
struct is_skip_base_annotation<skip_base> : std::true_type {};

template <typename T>
struct is_skip_field_annotation : std::false_type {};

template <>
struct is_skip_field_annotation<skip_field> : std::true_type {};

template <typename T>
constexpr inline bool skip_base_v = false;

template <std::meta::info Info, template <typename> typename Predicate>
consteval bool has_annotation() {
  static constexpr auto annotations = annotations_array<Info>();
  template for (constexpr auto annotation : annotations) {
    using annotation_t = remove_cvref_meta_type_t<annotation>;
    if constexpr (Predicate<annotation_t>::value) {
      return true;
    }
  }
  return false;
}

template <std::meta::info Member>
consteval std::string_view member_name() {
  static constexpr auto annotations = annotations_array<Member>();
  template for (constexpr auto annotation : annotations) {
    using annotation_t = remove_cvref_meta_type_t<annotation>;
    if constexpr (is_field_name_annotation<annotation_t>::value) {
      return annotation_t::value;
    }
  }
  return std::meta::identifier_of(Member);
}

template <typename T>
consteval std::string_view type_name() {
  static constexpr auto annotations = annotations_array<^^T>();
  template for (constexpr auto annotation : annotations) {
    using annotation_t = remove_cvref_meta_type_t<annotation>;
    if constexpr (is_struct_name_annotation<annotation_t>::value) {
      return annotation_t::value;
    }
  }
  return {};
}

template <std::meta::info Info>
consteval bool has_skip_base_annotation() {
  return has_annotation<Info, is_skip_base_annotation>();
}

template <std::meta::info Base>
consteval bool should_skip_base() {
  using base_type = meta_type_t<Base>;
  if constexpr (skip_base_v<std::remove_cvref_t<base_type>>) {
    return true;
  }
  else if constexpr (has_skip_base_annotation<Base>()) {
    return true;
  }
  else {
    return has_skip_base_annotation<std::meta::type_of(Base)>();
  }
}

template <std::meta::info Member>
consteval bool should_skip_field() {
  return has_annotation<Member, is_skip_field_annotation>();
}

template <std::meta::info Type>
consteval void append_data_members(std::vector<std::meta::info>& members) {
  constexpr auto ctx = std::meta::access_context::unchecked();
  static constexpr auto bases =
      std::define_static_array(std::meta::bases_of(Type, ctx));
  template for (constexpr auto base : bases) {
    if constexpr (!should_skip_base<base>()) {
      append_data_members<std::meta::type_of(base)>(members);
    }
  }
  static constexpr auto direct_members =
      std::define_static_array(std::meta::nonstatic_data_members_of(Type, ctx));
  template for (constexpr auto member : direct_members) {
    if constexpr (!should_skip_field<member>()) {
      members.push_back(member);
    }
  }
}

template <typename T>
consteval auto data_members() {
  std::vector<std::meta::info> members;
  append_data_members<^^T>(members);
  return members;
}

template <typename T>
consteval auto data_members_array() {
  return std::define_static_array(data_members<std::remove_cvref_t<T>>());
}

template <typename T>
consteval std::size_t members_count() {
  return data_members<T>().size();
}

template <typename T>
consteval auto member_names_array() {
  static constexpr auto members = data_members_array<T>();
  std::array<std::string_view, members.size()> names{};
  [[maybe_unused]] std::size_t index = 0;
  template for (constexpr auto member : members) {
    names[index++] = member_name<member>();
  }
  return names;
}

template <template <typename...> typename Predicate, typename T>
consteval std::size_t member_index_if() {
  static constexpr auto members = data_members_array<T>();
  std::size_t result = members.size();
  std::size_t index = 0;
  template for (constexpr auto member : members) {
    using member_t = remove_cvref_meta_type_t<member>;
    if constexpr (Predicate<member_t>::value) {
      if (result == members.size()) {
        result = index;
      }
    }
    ++index;
  }
  return result;
}

template <typename T, typename Visitor>
constexpr void for_each_data_member(T&& t, Visitor&& visitor) {
  static constexpr auto members = data_members_array<T>();
  [[maybe_unused]] std::size_t index = 0;
  template for (constexpr auto member : members) {
    visitor(t.[:member:], member_name<member>(), index++);
  }
}

template <typename T>
consteval auto member_offsets() {
  static constexpr auto members = data_members_array<T>();
  std::array<std::size_t, members.size()> offsets{};
  [[maybe_unused]] std::size_t index = 0;
  template for (constexpr auto member : members) {
    auto offset = std::meta::offset_of(member);
    offsets[index++] = static_cast<std::size_t>(offset.bytes);
  }
  return offsets;
}

}  // namespace ylt::reflection::reflect26

namespace ylt::reflection {
using reflect26::field_name;
using reflect26::fixed_string;
using reflect26::skip_base;
using reflect26::skip_field;
using reflect26::struct_name;
}  // namespace ylt::reflection
#endif  // YLT_USE_CXX26_REFLECTION
