#pragma once
#include <any>
#include <array>
#include <chrono>
#include <cstddef>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

#include "util.hpp"
#ifdef YLT_USE_CXX26_REFLECTION
#include <meta>

#include "ylt/reflection/reflect26_core.hpp"
#endif

namespace iguana {
struct iguana_adl_t {};

#if defined(__GNUC__) && !defined(__clang__)
#define IGUANA_DYNAMIC_NOINLINE __attribute__((noinline))
#else
#define IGUANA_DYNAMIC_NOINLINE
#endif

struct pb_field_annotation {
  size_t value;
};

constexpr pb_field_annotation pb_field(size_t field_no) { return {field_no}; }

struct pb_zigzag_annotation {};
struct pb_fixed_annotation {};
struct pb_bytes_annotation {};
template <size_t... Ns>
struct pb_oneof_annotation {
  static constexpr std::array<size_t, sizeof...(Ns)> values{Ns...};
};
struct pb_timestamp_annotation {};
struct pb_duration_annotation {};
struct pb_optional_annotation {};
struct pb_unknown_fields_annotation {};

inline constexpr pb_zigzag_annotation pb_zigzag{};
inline constexpr pb_fixed_annotation pb_fixed{};
inline constexpr pb_bytes_annotation pb_bytes{};
template <size_t... Ns>
inline constexpr pb_oneof_annotation<Ns...> pb_oneof{};
template <size_t... Ns>
inline constexpr pb_oneof_annotation<Ns...> oneof{};
inline constexpr pb_timestamp_annotation pb_as_timestamp{};
inline constexpr pb_timestamp_annotation as_timestamp{};
inline constexpr pb_duration_annotation pb_as_duration{};
inline constexpr pb_duration_annotation as_duration{};
inline constexpr pb_optional_annotation pb_optional{};
inline constexpr pb_unknown_fields_annotation pb_unknown_fields{};

struct pb_timestamp {
  int64_t seconds{};
  int32_t nanos{};

  pb_timestamp() = default;

  explicit pb_timestamp(std::chrono::system_clock::time_point value) {
    using namespace std::chrono;
    auto total = duration_cast<nanoseconds>(value.time_since_epoch());
    auto sec = duration_cast<std::chrono::seconds>(total);
    auto rem = total - sec;
    if (rem.count() < 0) {
      --sec;
      rem += std::chrono::seconds{1};
    }
    seconds = sec.count();
    nanos = static_cast<int32_t>(rem.count());
  }

  operator std::chrono::system_clock::time_point() const {
    using clock_duration = std::chrono::system_clock::duration;
    return std::chrono::system_clock::time_point{
        std::chrono::duration_cast<clock_duration>(
            std::chrono::seconds{seconds} + std::chrono::nanoseconds{nanos})};
  }
};

struct pb_duration {
  int64_t seconds{};
  int32_t nanos{};

  pb_duration() = default;

  explicit pb_duration(std::chrono::nanoseconds value) {
    auto sec = std::chrono::duration_cast<std::chrono::seconds>(value);
    auto rem = value - sec;
    seconds = sec.count();
    nanos = static_cast<int32_t>(rem.count());
  }

  operator std::chrono::nanoseconds() const {
    return std::chrono::seconds{seconds} + std::chrono::nanoseconds{nanos};
  }
};
YLT_REFL(pb_timestamp, seconds, nanos);
YLT_REFL(pb_duration, seconds, nanos);

namespace detail {
#ifdef YLT_USE_CXX26_REFLECTION
namespace reflect26 = ylt::reflection::reflect26;
#endif

template <typename T>
struct identity {};

struct field_info {
  size_t offset;
  std::string_view type_name;
};

struct base {
  virtual void to_pb(std::string& str) const {}
  virtual void from_pb(std::string_view str) {}
  virtual void to_xml(std::string& str) const {}
  virtual void from_xml(std::string_view str) {}
  virtual void to_json(std::string& str) const {}
  virtual void from_json(std::string_view str) {}
  virtual void to_yaml(std::string& str) const {}
  virtual void from_yaml(std::string_view str) {}
  virtual std::vector<std::string_view> get_fields_name() const { return {}; }
  virtual std::any get_field_any(std::string_view name) const { return {}; }
  virtual iguana::detail::field_info get_field_info(
      std::string_view name) const {
    return {};
  }

  template <typename T>
  T& get_field_value(std::string_view name) {
    auto info = get_field_info(name);
    check_field<T>(name, info);
    auto ptr = static_cast<char*>(object_ptr()) + info.offset;
    return *((T*)ptr);
  }

  template <typename T, typename FiledType = T>
  IGUANA_DYNAMIC_NOINLINE void set_field_value(std::string_view name, T val) {
    auto info = get_field_info(name);
    check_field<FiledType>(name, info);

    auto ptr = static_cast<char*>(object_ptr()) + info.offset;

    static_assert(std::is_constructible_v<FiledType, T>, "can not assign");

    *((FiledType*)ptr) = std::move(val);
  }
  virtual ~base() {}

 protected:
  virtual void* object_ptr() { return this; }
  virtual const void* object_ptr() const { return this; }

 private:
  template <typename T>
  void check_field(std::string_view name, const field_info& info) {
    if (info.offset == 0) {
      throw std::invalid_argument(std::string(name) + " field not exist ");
    }

#if defined(__clang__) || defined(_MSC_VER) || \
    (defined(__GNUC__) && __GNUC__ > 8)
    if (info.type_name != iguana::type_string<T>()) {
      std::string str = "type is not match: can not assign ";
      str.append(iguana::type_string<T>());
      str.append(" to ").append(info.type_name);

      throw std::invalid_argument(str);
    }
#endif
  }
};

inline std::unordered_map<std::string_view,
                          std::function<std::shared_ptr<base>()>>
    g_pb_map;

template <typename T>
struct field_type_t;

template <typename... Args>
struct field_type_t<std::tuple<Args...>> {
  using value_type = std::variant<Args...>;
};

template <typename T>
constexpr size_t count_variant_size() {
  if constexpr (is_variant<T>::value) {
    using first_type = std::variant_alternative_t<0, T>;
    if constexpr (std::is_same_v<first_type, std::monostate>) {
      return std::variant_size_v<T> - 1;
    }
    else {
      return std::variant_size_v<T>;
    }
  }
  else {
    return 1;
  }
}

template <typename T, size_t... I>
constexpr size_t tuple_type_count_impl(std::index_sequence<I...>) {
  return (
      (count_variant_size<member_value_type_t<std::tuple_element_t<I, T>>>() +
       ...));
}

template <typename T>
constexpr size_t tuple_type_count() {
  return tuple_type_count_impl<T>(
      std::make_index_sequence<std::tuple_size_v<T>>{});
}

template <typename T, size_t Size, typename Tuple, size_t... I>
constexpr auto inline get_members_impl(Tuple&& tp, std::index_sequence<I...>) {
  return frozen::unordered_map<uint32_t, T, sizeof...(I)>{
      {std::get<I>(tp).field_no,
       T{std::in_place_index<I>, std::move(std::get<I>(tp))}}...};
}

template <typename T, typename = void>
struct is_custom_reflection : std::false_type {};

template <typename T>
struct is_custom_reflection<
    T, std::void_t<decltype(get_members_impl(std::declval<T*>()))>>
    : std::true_type {};

template <typename T>
inline constexpr bool is_custom_reflection_v =
    is_custom_reflection<ylt::reflection::remove_cvref_t<T>>::value;

// owner_type: parant type, value_type: member value type, SubType: subtype from
// variant
template <typename Owner, typename Value, size_t FieldNo,
          typename ElementType = Value, bool BytesSchema = false,
          bool TimestampSchema = false, bool DurationSchema = false,
          bool OptionalSchema = false, bool ZigzagSchema = false,
          bool FixedSchema = false, typename WireValue = Value,
          typename WireElement = ElementType>
struct pb_field_t {
  using owner_type = ylt::reflection::remove_cvref_t<Owner>;
  using value_type = Value;
  using sub_type = ElementType;
  using wire_value_type = WireValue;
  using wire_sub_type = WireElement;

  // constexpr pb_field_t() = default;
  auto& value(owner_type& value) const {
    auto member_ptr = (value_type*)((char*)(&value) + offset);
    return *member_ptr;
  }
  auto const& value(const owner_type& value) const {
    auto member_ptr = (value_type*)((char*)(&value) + offset);
    return *member_ptr;
  }

  size_t offset;
  std::string_view field_name;

  inline static constexpr uint32_t field_no = FieldNo;
  inline static constexpr bool bytes_schema = BytesSchema;
  inline static constexpr bool timestamp_schema = TimestampSchema;
  inline static constexpr bool duration_schema = DurationSchema;
  inline static constexpr bool optional_schema = OptionalSchema;
  inline static constexpr bool zigzag_schema = ZigzagSchema;
  inline static constexpr bool fixed_schema = FixedSchema;
};

template <typename>
struct pb_field_no;

template <typename Owner, typename Value, size_t FieldNo, typename ElementType,
          bool BytesSchema, bool TimestampSchema, bool DurationSchema,
          bool OptionalSchema, bool ZigzagSchema, bool FixedSchema,
          typename WireValue, typename WireElement>
struct pb_field_no<
    pb_field_t<Owner, Value, FieldNo, ElementType, BytesSchema, TimestampSchema,
               DurationSchema, OptionalSchema, ZigzagSchema, FixedSchema,
               WireValue, WireElement>> {
  static constexpr size_t value = FieldNo;
};

constexpr bool is_valid_pb_field_no(size_t field_no) {
  constexpr size_t max_field_no = (size_t{1} << 29) - 1;
  return field_no > 0 && field_no <= max_field_no &&
         (field_no < 19000 || field_no > 19999);
}

template <typename Tuple, size_t... I>
constexpr bool has_invalid_field_nos(std::index_sequence<I...>) {
  return ((!is_valid_pb_field_no(
              pb_field_no<std::tuple_element_t<I, Tuple>>::value)) ||
          ...);
}

template <typename Tuple, size_t... I>
constexpr bool has_duplicate_field_nos(std::index_sequence<I...>) {
  if constexpr (sizeof...(I) == 0) {
    return false;
  }
  else {
    constexpr size_t nos[] = {
        pb_field_no<std::tuple_element_t<I, Tuple>>::value...};
    constexpr size_t N = sizeof...(I);
    for (size_t i = 0; i < N; ++i)
      for (size_t j = i + 1; j < N; ++j)
        if (nos[i] == nos[j])
          return true;
    return false;
  }
}

template <typename Tuple>
constexpr void validate_pb_members_tuple() {
  constexpr size_t N = std::tuple_size_v<Tuple>;
  static_assert(!has_invalid_field_nos<Tuple>(std::make_index_sequence<N>{}),
                "protobuf field numbers must be in [1, 2^29 - 1] and not in "
                "[19000, 19999]");
  static_assert(!has_duplicate_field_nos<Tuple>(std::make_index_sequence<N>{}),
                "duplicate proto field numbers detected");
}

template <size_t I, typename ValueType, typename Array>
constexpr inline auto get_field_no_impl(Array& arr, size_t& index) {
  arr[I] = index;
  if constexpr (is_variant<ValueType>::value) {
    constexpr size_t variant_size = count_variant_size<ValueType>();
    index += variant_size;
  }
  else {
    index++;
    return;
  }
}

template <typename Tuple, size_t... I>
inline constexpr auto get_field_no(std::index_sequence<I...>) {
  std::array<size_t, sizeof...(I)> arr{};
  size_t index = 0;
  (get_field_no_impl<
       I, ylt::reflection::remove_cvref_t<std::tuple_element_t<I, Tuple>>>(
       arr, index),
   ...);
  return arr;
}

template <typename Variant>
constexpr size_t pb_variant_first_case_index() {
  using first_type = std::variant_alternative_t<0, Variant>;
  if constexpr (std::is_same_v<first_type, std::monostate>) {
    return 1;
  }
  else {
    return 0;
  }
}

template <typename Variant>
constexpr size_t pb_variant_case_count() {
  return std::variant_size_v<Variant> - pb_variant_first_case_index<Variant>();
}

template <typename T, typename value_type, size_t field_no, size_t First,
          size_t... I>
constexpr inline auto build_pb_variant_fields(size_t offset,
                                              std::string_view name,
                                              std::index_sequence<I...>) {
  return std::tuple(
      pb_field_t<T, value_type, field_no + I + 1,
                 std::variant_alternative_t<I + First, value_type>>{offset,
                                                                    name}...);
}

template <typename T, typename ValueType, size_t First, size_t... FieldNos,
          size_t... I>
constexpr inline auto build_pb_oneof_fields_impl(size_t offset,
                                                 std::string_view name,
                                                 std::index_sequence<I...>) {
  using U = ylt::reflection::remove_cvref_t<T>;
  using value_type = ylt::reflection::remove_cvref_t<ValueType>;
  return std::tuple(
      pb_field_t<U, value_type, FieldNos,
                 std::variant_alternative_t<I + First, value_type>>{offset,
                                                                    name}...);
}

template <typename T, typename ValueType, size_t... FieldNos>
constexpr inline auto build_pb_oneof_fields(size_t offset,
                                            std::string_view name) {
  using value_type = ylt::reflection::remove_cvref_t<ValueType>;
  if constexpr (!is_variant<value_type>::value) {
    static_assert(is_variant<value_type>::value,
                  "pb_oneof_field member must be std::variant");
    return std::tuple<>{};
  }
  else if constexpr (!std::is_same_v<std::variant_alternative_t<0, value_type>,
                                     std::monostate>) {
    static_assert(std::is_same_v<std::variant_alternative_t<0, value_type>,
                                 std::monostate>,
                  "pb_oneof_field member must start with std::monostate");
    return std::tuple<>{};
  }
  else if constexpr (sizeof...(FieldNos) !=
                     pb_variant_case_count<value_type>()) {
    static_assert(sizeof...(FieldNos) == pb_variant_case_count<value_type>(),
                  "pb_oneof_field field number count must match variant "
                  "alternatives excluding std::monostate");
    return std::tuple<>{};
  }
  else {
    static_assert(
        (is_valid_pb_field_no(FieldNos) && ...),
        "protobuf oneof field numbers must be in [1, 2^29 - 1] and not in "
        "[19000, 19999]");
    constexpr size_t first = pb_variant_first_case_index<value_type>();
    return build_pb_oneof_fields_impl<T, value_type, first, FieldNos...>(
        offset, name, std::make_index_sequence<sizeof...(FieldNos)>{});
  }
}

template <typename T, size_t field_no, typename ValueType,
          bool BytesSchema = false, bool TimestampSchema = false,
          bool DurationSchema = false, bool OptionalSchema = false,
          bool ZigzagSchema = false, bool FixedSchema = false,
          typename WireValueType = ValueType>
constexpr inline auto build_pb_fields_impl(size_t offset,
                                           std::string_view name) {
  using value_type = ylt::reflection::remove_cvref_t<ValueType>;
  using wire_value_type = ylt::reflection::remove_cvref_t<WireValueType>;
  using U = std::remove_reference_t<T>;

  if constexpr (is_variant<value_type>::value) {
    constexpr size_t first = pb_variant_first_case_index<value_type>();
    constexpr size_t variant_size = pb_variant_case_count<value_type>();
    return build_pb_variant_fields<U, value_type, field_no, first>(
        offset, name, std::make_index_sequence<variant_size>{});
  }
  else {
    return std::tuple(
        pb_field_t<U, value_type, field_no + 1, value_type, BytesSchema,
                   TimestampSchema, DurationSchema, OptionalSchema,
                   ZigzagSchema, FixedSchema, wire_value_type, wire_value_type>{
            offset, name});
  }
}

template <bool Zigzag, bool Fixed, typename T>
struct pb_wire_type_selector {
  using type = T;
};

template <>
struct pb_wire_type_selector<true, false, int32_t> {
  using type = iguana::sint32_t;
};

template <>
struct pb_wire_type_selector<true, false, int64_t> {
  using type = iguana::sint64_t;
};

template <>
struct pb_wire_type_selector<false, true, uint32_t> {
  using type = iguana::fixed32_t;
};

template <>
struct pb_wire_type_selector<false, true, uint64_t> {
  using type = iguana::fixed64_t;
};

template <>
struct pb_wire_type_selector<false, true, int32_t> {
  using type = iguana::sfixed32_t;
};

template <>
struct pb_wire_type_selector<false, true, int64_t> {
  using type = iguana::sfixed64_t;
};

template <bool Zigzag, bool Fixed, typename T>
struct pb_wire_type_selector<Zigzag, Fixed, std::optional<T>> {
  using type = std::optional<typename pb_wire_type_selector<
      Zigzag, Fixed, ylt::reflection::remove_cvref_t<T>>::type>;
};

template <bool Zigzag, bool Fixed, typename T, typename Alloc>
struct pb_wire_type_selector<Zigzag, Fixed, std::vector<T, Alloc>> {
  using type = std::vector<typename pb_wire_type_selector<
      Zigzag, Fixed, ylt::reflection::remove_cvref_t<T>>::type>;
};

template <typename T>
struct pb_annotation_leaf_type {
  using type = ylt::reflection::remove_cvref_t<T>;
};

template <typename T>
struct pb_annotation_leaf_type<std::optional<T>> {
  using type = ylt::reflection::remove_cvref_t<T>;
};

template <typename T, typename Alloc>
struct pb_annotation_leaf_type<std::vector<T, Alloc>> {
  using type = ylt::reflection::remove_cvref_t<T>;
};

template <typename T>
using pb_annotation_leaf_type_t =
    typename pb_annotation_leaf_type<ylt::reflection::remove_cvref_t<T>>::type;

template <typename T>
struct is_pb_zigzag_option : std::false_type {};

template <>
struct is_pb_zigzag_option<iguana::pb_zigzag_annotation> : std::true_type {};

template <typename T>
struct is_pb_fixed_option : std::false_type {};

template <>
struct is_pb_fixed_option<iguana::pb_fixed_annotation> : std::true_type {};

template <typename T>
struct is_pb_bytes_option : std::false_type {};

template <>
struct is_pb_bytes_option<iguana::pb_bytes_annotation> : std::true_type {};

template <typename T>
struct is_pb_timestamp_option : std::false_type {};

template <>
struct is_pb_timestamp_option<iguana::pb_timestamp_annotation>
    : std::true_type {};

template <typename T>
struct is_pb_duration_option : std::false_type {};

template <>
struct is_pb_duration_option<iguana::pb_duration_annotation> : std::true_type {
};

template <typename T>
struct is_pb_optional_option : std::false_type {};

template <>
struct is_pb_optional_option<iguana::pb_optional_annotation> : std::true_type {
};

template <typename... Options>
struct pb_schema_options {
  static constexpr bool bytes =
      (is_pb_bytes_option<std::remove_cvref_t<Options>>::value || ...);
  static constexpr bool timestamp =
      (is_pb_timestamp_option<std::remove_cvref_t<Options>>::value || ...);
  static constexpr bool duration =
      (is_pb_duration_option<std::remove_cvref_t<Options>>::value || ...);
  static constexpr bool optional =
      (is_pb_optional_option<std::remove_cvref_t<Options>>::value || ...);
  static constexpr bool zigzag =
      (is_pb_zigzag_option<std::remove_cvref_t<Options>>::value || ...);
  static constexpr bool fixed =
      (is_pb_fixed_option<std::remove_cvref_t<Options>>::value || ...);
};

template <typename Owner, typename Value>
struct pb_unknown_fields_t {
  using owner_type = ylt::reflection::remove_cvref_t<Owner>;
  using value_type = Value;

  auto& value(owner_type& value) const {
    auto member_ptr = (value_type*)((char*)(&value) + offset);
    return *member_ptr;
  }
  auto const& value(const owner_type& value) const {
    auto member_ptr = (value_type*)((char*)(&value) + offset);
    return *member_ptr;
  }

  size_t offset;
  std::string_view field_name;
};

template <typename T>
struct is_pb_unknown_fields_descriptor : std::false_type {};

template <typename Owner, typename Value>
struct is_pb_unknown_fields_descriptor<pb_unknown_fields_t<Owner, Value>>
    : std::true_type {};

template <typename T>
inline constexpr bool is_pb_unknown_fields_descriptor_v =
    is_pb_unknown_fields_descriptor<ylt::reflection::remove_cvref_t<T>>::value;

template <typename T>
IGUANA_INLINE auto pb_member_tuple_item(T&& value) {
  if constexpr (is_pb_unknown_fields_descriptor_v<T>) {
    return std::tuple<>{};
  }
  else {
    return std::make_tuple(std::forward<T>(value));
  }
}

template <typename Tuple, size_t... I>
IGUANA_INLINE auto filter_pb_member_tuple_impl(Tuple&& tp,
                                               std::index_sequence<I...>) {
  return std::tuple_cat(
      pb_member_tuple_item(std::get<I>(std::forward<Tuple>(tp)))...);
}

template <typename Tuple>
IGUANA_INLINE auto filter_pb_member_tuple(Tuple&& tp) {
  using tuple_type = ylt::reflection::remove_cvref_t<Tuple>;
  return filter_pb_member_tuple_impl(
      std::forward<Tuple>(tp),
      std::make_index_sequence<std::tuple_size_v<tuple_type>>{});
}

template <typename Tuple, size_t... I>
constexpr size_t pb_unknown_fields_count_tuple(std::index_sequence<I...>) {
  return ((is_pb_unknown_fields_descriptor_v<std::tuple_element_t<I, Tuple>>
               ? size_t{1}
               : size_t{0}) +
          ...);
}

template <typename Tuple>
constexpr size_t pb_unknown_fields_count_tuple() {
  constexpr size_t N = std::tuple_size_v<Tuple>;
  if constexpr (N == 0) {
    return 0;
  }
  else {
    return pb_unknown_fields_count_tuple<Tuple>(std::make_index_sequence<N>{});
  }
}

template <typename T, typename Field, typename Func>
IGUANA_INLINE void visit_pb_unknown_field(T&& t, Field&& field, Func&& func) {
  using field_type = ylt::reflection::remove_cvref_t<Field>;
  if constexpr (is_pb_unknown_fields_descriptor_v<field_type>) {
    using value_type = typename field_type::value_type;
    static_assert(std::is_same_v<value_type, std::string>,
                  "pb_unknown_fields member must be std::string");
    func(field.value(t));
  }
}

template <typename T, typename Tuple, typename Func, size_t... I>
IGUANA_INLINE void visit_pb_unknown_fields_tuple(T&& t, Tuple&& tp, Func&& func,
                                                 std::index_sequence<I...>) {
  (visit_pb_unknown_field(t, std::get<I>(tp), func), ...);
}

template <typename T, typename Func>
IGUANA_INLINE void visit_pb_unknown_fields_custom(T&& t, Func&& func) {
  using U = ylt::reflection::remove_cvref_t<T>;
  if constexpr (is_custom_reflection_v<U>) {
    auto tp = get_members_impl((U*)nullptr);
    using Tuple = ylt::reflection::remove_cvref_t<decltype(tp)>;
    static_assert(pb_unknown_fields_count_tuple<Tuple>() <= 1,
                  "only one pb_unknown_fields member is supported");
    visit_pb_unknown_fields_tuple(
        t, tp, std::forward<Func>(func),
        std::make_index_sequence<std::tuple_size_v<Tuple>>{});
  }
}

#ifdef YLT_USE_CXX26_REFLECTION
template <typename T>
struct is_pb_field_annotation : std::false_type {};

template <>
struct is_pb_field_annotation<iguana::pb_field_annotation> : std::true_type {};

template <typename T>
struct is_pb_zigzag_annotation : std::false_type {};

template <>
struct is_pb_zigzag_annotation<iguana::pb_zigzag_annotation> : std::true_type {
};

template <typename T>
struct is_pb_fixed_annotation : std::false_type {};

template <>
struct is_pb_fixed_annotation<iguana::pb_fixed_annotation> : std::true_type {};

template <typename T>
struct is_pb_bytes_annotation : std::false_type {};

template <>
struct is_pb_bytes_annotation<iguana::pb_bytes_annotation> : std::true_type {};

template <typename T>
struct is_pb_oneof_annotation : std::false_type {};

template <size_t... Ns>
struct is_pb_oneof_annotation<iguana::pb_oneof_annotation<Ns...>>
    : std::true_type {};

template <typename T>
struct is_pb_timestamp_annotation : std::false_type {};

template <>
struct is_pb_timestamp_annotation<iguana::pb_timestamp_annotation>
    : std::true_type {};

template <typename T>
struct is_pb_duration_annotation : std::false_type {};

template <>
struct is_pb_duration_annotation<iguana::pb_duration_annotation>
    : std::true_type {};

template <typename T>
struct is_pb_optional_annotation : std::false_type {};

template <>
struct is_pb_optional_annotation<iguana::pb_optional_annotation>
    : std::true_type {};

template <typename T>
struct is_pb_unknown_fields_annotation : std::false_type {};

template <>
struct is_pb_unknown_fields_annotation<iguana::pb_unknown_fields_annotation>
    : std::true_type {};

template <std::meta::info Member>
consteval bool has_pb_zigzag() {
  return reflect26::has_annotation<Member, is_pb_zigzag_annotation>();
}

template <std::meta::info Member>
consteval bool has_pb_fixed() {
  return reflect26::has_annotation<Member, is_pb_fixed_annotation>();
}

template <std::meta::info Member>
consteval bool has_pb_unknown_fields() {
  return reflect26::has_annotation<Member, is_pb_unknown_fields_annotation>();
}

template <std::meta::info Member>
consteval bool has_pb_bytes() {
  return reflect26::has_annotation<Member, is_pb_bytes_annotation>();
}

template <std::meta::info Member>
consteval bool has_pb_oneof() {
  return reflect26::has_annotation<Member, is_pb_oneof_annotation>();
}

template <std::meta::info Member>
consteval bool has_pb_as_timestamp() {
  return reflect26::has_annotation<Member, is_pb_timestamp_annotation>();
}

template <std::meta::info Member>
consteval bool has_pb_as_duration() {
  return reflect26::has_annotation<Member, is_pb_duration_annotation>();
}

template <std::meta::info Member>
consteval bool has_pb_optional() {
  return reflect26::has_annotation<Member, is_pb_optional_annotation>();
}

template <std::meta::info Member>
consteval size_t pb_oneof_count() {
  static constexpr auto annotations = reflect26::annotations_array<Member>();
  template for (constexpr auto annotation : annotations) {
    using annotation_t = reflect26::remove_cvref_meta_type_t<annotation>;
    if constexpr (is_pb_oneof_annotation<annotation_t>::value) {
      return annotation_t::values.size();
    }
  }
  return 0;
}

template <std::meta::info Member, size_t I>
consteval size_t pb_oneof_field_no() {
  static constexpr auto annotations = reflect26::annotations_array<Member>();
  template for (constexpr auto annotation : annotations) {
    using annotation_t = reflect26::remove_cvref_meta_type_t<annotation>;
    if constexpr (is_pb_oneof_annotation<annotation_t>::value) {
      constexpr auto field_no = annotation_t::values[I];
      static_assert(field_no > 0,
                    "protobuf oneof field number must be positive");
      static_assert(field_no <= ((size_t{1} << 29) - 1),
                    "protobuf oneof field number exceeds 2^29 - 1");
      static_assert(field_no < 19000 || field_no > 19999,
                    "protobuf oneof field number is in reserved range "
                    "[19000, 19999]");
      return field_no;
    }
  }
  return 0;
}

template <std::meta::info Member, typename ValueType>
struct pb_annotation_wire_type {
  using value_type = ylt::reflection::remove_cvref_t<ValueType>;
  static_assert(!(has_pb_zigzag<Member>() && has_pb_fixed<Member>()),
                "protobuf field can't use both pb_zigzag and pb_fixed");
  using type =
      typename pb_wire_type_selector<has_pb_zigzag<Member>(),
                                     has_pb_fixed<Member>(), value_type>::type;
};

template <typename T>
consteval size_t pb_unknown_fields_count() {
  using U = ylt::reflection::remove_cvref_t<T>;
  static constexpr auto members = reflect26::data_members_array<U>();
  size_t count = 0;
  template for (constexpr auto member : members) {
    if constexpr (has_pb_unknown_fields<member>()) {
      ++count;
    }
  }
  return count;
}

template <typename T, typename Func>
IGUANA_INLINE void visit_pb_unknown_fields_annotation(T&& t, Func&& func) {
  using U = ylt::reflection::remove_cvref_t<T>;
  static_assert(pb_unknown_fields_count<U>() <= 1,
                "only one pb_unknown_fields member is supported");
  static constexpr auto members = reflect26::data_members_array<U>();
  template for (constexpr auto member : members) {
    if constexpr (has_pb_unknown_fields<member>()) {
      using field_type =
          ylt::reflection::remove_cvref_t<decltype(t.[:member:])>;
      static_assert(std::is_same_v<field_type, std::string>,
                    "pb_unknown_fields member must be std::string");
      func(t.[:member:]);
    }
  }
}

template <typename T, size_t I>
consteval size_t pb_field_width() {
  using U = ylt::reflection::remove_cvref_t<T>;
  static constexpr auto members = reflect26::data_members_array<U>();
  if constexpr (has_pb_unknown_fields<members[I]>()) {
    return 0;
  }
  else {
    using value_type = reflect26::remove_cvref_meta_type_t<members[I]>;
    if constexpr (is_variant<value_type>::value) {
      if constexpr (has_pb_oneof<members[I]>()) {
        return pb_oneof_count<members[I]>();
      }
      else {
        return pb_variant_case_count<value_type>();
      }
    }
    else {
      return 1;
    }
  }
}

template <typename T, size_t I>
consteval size_t pb_default_field_index() {
  if constexpr (I == 0) {
    return 0;
  }
  else {
    return pb_default_field_index<T, I - 1>() + pb_field_width<T, I - 1>();
  }
}

template <std::meta::info Member>
consteval size_t pb_annotation_field_no() {
  static constexpr auto annotations = reflect26::annotations_array<Member>();
  template for (constexpr auto annotation : annotations) {
    using annotation_t = reflect26::remove_cvref_meta_type_t<annotation>;
    if constexpr (is_pb_field_annotation<annotation_t>::value) {
      constexpr auto field_no =
          std::meta::extract<iguana::pb_field_annotation>(annotation).value;
      static_assert(field_no > 0, "protobuf field number must be positive");
      static_assert(field_no <= ((size_t{1} << 29) - 1),
                    "protobuf field number exceeds 2^29 - 1");
      static_assert(field_no < 19000 || field_no > 19999,
                    "protobuf field number is in reserved range "
                    "[19000, 19999]");
      return field_no;
    }
  }
  return 0;
}

template <typename T, size_t I, size_t DefaultIndex>
consteval size_t pb_field_index() {
  using U = ylt::reflection::remove_cvref_t<T>;
  static constexpr auto members = reflect26::data_members_array<U>();
  if constexpr (I < members.size()) {
    constexpr auto field_no = pb_annotation_field_no<members[I]>();
    if constexpr (field_no > 0) {
      return field_no - 1;
    }
  }
  return DefaultIndex;
}

template <typename T, size_t I, typename ValueType>
struct pb_field_value_type {
  using U = ylt::reflection::remove_cvref_t<T>;
  static constexpr auto members = reflect26::data_members_array<U>();
  using type = typename pb_annotation_wire_type<members[I], ValueType>::type;
};

template <typename T, typename ValueType, std::meta::info Member, size_t... I>
constexpr inline auto build_pb_annotation_oneof_fields(
    size_t offset, std::string_view name, std::index_sequence<I...>) {
  using U = ylt::reflection::remove_cvref_t<T>;
  using value_type = ylt::reflection::remove_cvref_t<ValueType>;
  constexpr size_t first = pb_variant_first_case_index<value_type>();
  return std::tuple(
      pb_field_t<U, value_type, pb_oneof_field_no<Member, I>(),
                 std::variant_alternative_t<I + first, value_type>>{offset,
                                                                    name}...);
}

template <typename T, size_t I, typename ValueType, typename Array>
inline auto build_pb_annotation_field(const Array& offset_arr,
                                      std::string_view name) {
  using U = ylt::reflection::remove_cvref_t<T>;
  static constexpr auto members = reflect26::data_members_array<U>();
  if constexpr (has_pb_unknown_fields<members[I]>()) {
    using value_type = ylt::reflection::remove_cvref_t<ValueType>;
    static_assert(std::is_same_v<value_type, std::string>,
                  "pb_unknown_fields member must be std::string");
    return std::tuple<>{};
  }
  else {
    constexpr size_t default_index = pb_default_field_index<U, I>();
    using value_type = ylt::reflection::remove_cvref_t<ValueType>;
    using annotation_leaf_type = pb_annotation_leaf_type_t<value_type>;
    if constexpr (has_pb_oneof<members[I]>()) {
      static_assert(is_variant<value_type>::value,
                    "pb_oneof member must be std::variant");
      static_assert(std::is_same_v<std::variant_alternative_t<0, value_type>,
                                   std::monostate>,
                    "pb_oneof member must start with std::monostate");
      static_assert(
          pb_oneof_count<members[I]>() == pb_variant_case_count<value_type>(),
          "pb_oneof field number count must match variant "
          "alternatives excluding std::monostate");
      return build_pb_annotation_oneof_fields<T, ValueType, members[I]>(
          offset_arr[I], name,
          std::make_index_sequence<pb_oneof_count<members[I]>()>{});
    }
    else {
      static_assert(!(has_pb_as_timestamp<members[I]>() &&
                      has_pb_as_duration<members[I]>()),
                    "protobuf field can't use both pb_as_timestamp and "
                    "pb_as_duration");
      static_assert(!has_pb_as_timestamp<members[I]>() ||
                        std::is_same_v<annotation_leaf_type,
                                       std::chrono::system_clock::time_point>,
                    "pb_as_timestamp member must be "
                    "std::chrono::system_clock::time_point, or optional/vector "
                    "of that type");
      static_assert(
          !has_pb_as_duration<members[I]>() ||
              std::is_same_v<annotation_leaf_type, std::chrono::nanoseconds>,
          "pb_as_duration member must be std::chrono::nanoseconds, "
          "or optional/vector of that type");
      static_assert(!has_pb_bytes<members[I]>() ||
                        std::is_same_v<annotation_leaf_type, std::string> ||
                        std::is_same_v<annotation_leaf_type, std::string_view>,
                    "pb_bytes member must be std::string, std::string_view, "
                    "or optional/vector of that type");
      static_assert(!has_pb_optional<members[I]>() || optional_v<value_type>,
                    "pb_optional member must be std::optional<T>");
      static_assert(!has_pb_zigzag<members[I]>() ||
                        std::is_same_v<annotation_leaf_type, int32_t> ||
                        std::is_same_v<annotation_leaf_type, int64_t>,
                    "pb_zigzag member must be int32_t/int64_t, or "
                    "optional/vector of that type");
      static_assert(!has_pb_fixed<members[I]>() ||
                        std::is_same_v<annotation_leaf_type, uint32_t> ||
                        std::is_same_v<annotation_leaf_type, uint64_t> ||
                        std::is_same_v<annotation_leaf_type, int32_t> ||
                        std::is_same_v<annotation_leaf_type, int64_t>,
                    "pb_fixed member must be 32/64-bit int, or "
                    "optional/vector of that type");
      return build_pb_fields_impl<
          T, pb_field_index<T, I, default_index>(), ValueType,
          has_pb_bytes<members[I]>(), has_pb_as_timestamp<members[I]>(),
          has_pb_as_duration<members[I]>(), has_pb_optional<members[I]>(),
          has_pb_zigzag<members[I]>(), has_pb_fixed<members[I]>(),
          typename pb_field_value_type<T, I, ValueType>::type>(offset_arr[I],
                                                               name);
    }
  }
}
#endif

template <typename T, typename Func>
IGUANA_INLINE void visit_pb_unknown_fields(T&& t, Func&& func) {
  using U = ylt::reflection::remove_cvref_t<T>;
  if constexpr (is_custom_reflection_v<U>) {
    visit_pb_unknown_fields_custom(t, std::forward<Func>(func));
  }
#ifdef YLT_USE_CXX26_REFLECTION
  else if constexpr (ylt_refletable_v<U>) {
    visit_pb_unknown_fields_annotation(t, std::forward<Func>(func));
  }
#endif
}

template <typename T>
IGUANA_INLINE size_t pb_unknown_fields_size(const T& t) {
  size_t size = 0;
  visit_pb_unknown_fields(t, [&](const std::string& fields) {
    size += fields.size();
  });
  return size;
}

template <typename T, typename Writer>
IGUANA_INLINE void write_pb_unknown_fields(const T& t, Writer& writer) {
  visit_pb_unknown_fields(t, [&](const std::string& fields) {
    writer.write(fields.data(), fields.size());
  });
}

template <typename T>
IGUANA_INLINE void append_pb_unknown_field(T& t, const char* data,
                                           size_t size) {
  visit_pb_unknown_fields(t, [&](std::string& fields) {
    fields.append(data, size);
  });
}

#ifdef YLT_USE_CXX26_REFLECTION
template <typename T, typename Array, size_t... I>
inline auto build_pb_fields(const Array& offset_arr,
                            std::index_sequence<I...>) {
  constexpr auto arr = ylt::reflection::get_member_names<T>();
  using U = ylt::reflection::remove_cvref_t<T>;
  static constexpr auto members = reflect26::data_members_array<U>();
  return std::tuple_cat(
      build_pb_annotation_field<T, I, reflect26::meta_type_t<members[I]>>(
          offset_arr, arr[I])...);
}
#else
template <typename Tuple, typename T, typename Array, size_t... I>
inline auto build_pb_fields(const Array& offset_arr,
                            std::index_sequence<I...>) {
  constexpr auto arr = ylt::reflection::get_member_names<T>();
  constexpr std::array<size_t, sizeof...(I)> indexs =
      get_field_no<Tuple>(std::make_index_sequence<sizeof...(I)>{});
  return std::tuple_cat(
      build_pb_fields_impl<T, indexs[I], std::tuple_element_t<I, Tuple>>(
          offset_arr[I], arr[I])...);
}
#endif

template <typename T>
inline auto get_pb_members_tuple(T&& t) {
  using U = ylt::reflection::remove_cvref_t<T>;
  if constexpr (is_custom_reflection_v<U>) {
    auto raw = get_members_impl((U*)nullptr);
    auto res = filter_pb_member_tuple(std::move(raw));
    using ResultTuple = decltype(res);
    validate_pb_members_tuple<ResultTuple>();
    return res;
  }
  else if constexpr (ylt_refletable_v<U>) {
#ifdef YLT_USE_CXX26_REFLECTION
    static const auto& offset_arr =
        ylt::reflection::internal::get_member_offset_arr<U>();
    constexpr auto count = ylt::reflection::members_count_v<U>;
    auto res =
        build_pb_fields<T>(offset_arr, std::make_index_sequence<count>{});
#else
    static auto& offset_arr = ylt::reflection::internal::get_member_offset_arr(
        ylt::reflection::internal::wrapper<U>::value);
    using Tuple = decltype(ylt::reflection::object_to_tuple(std::declval<U>()));
    auto res = build_pb_fields<Tuple, T>(
        offset_arr, std::make_index_sequence<std::tuple_size_v<Tuple>>{});
#endif
    using ResultTuple = decltype(res);
    validate_pb_members_tuple<ResultTuple>();
    return res;
  }
  else {
    static_assert(!sizeof(T), "not a reflectable type");
  }
}

template <typename T>
inline auto get_members(T&& t) {
  if constexpr (ylt_refletable_v<T> || is_custom_reflection_v<T>) {
    static auto tp = get_pb_members_tuple(std::forward<T>(t));
    using Tuple = std::decay_t<decltype(tp)>;
    using value_type = typename field_type_t<Tuple>::value_type;
    constexpr auto Size = tuple_type_count<Tuple>();
    return get_members_impl<value_type, Size>(tp,
                                              std::make_index_sequence<Size>{});
  }
  else {
    static_assert(!sizeof(T), "expected reflection or custom reflection");
  }
}

}  // namespace detail

template <typename T>
inline bool register_type() {
#if defined(__clang__) || defined(_MSC_VER) || \
    (defined(__GNUC__) && __GNUC__ > 8)
  if constexpr (std::is_base_of_v<detail::base, T>) {
    auto it = detail::g_pb_map.emplace(type_string<T>(), [] {
      return std::make_shared<T>();
    });
    return it.second;
  }
  else {
    return true;
  }
#else
  return true;
#endif
}

template <typename T, typename U>
IGUANA_INLINE constexpr size_t member_offset(T* t, U T::*member) {
  return (char*)&(t->*member) - (char*)t;
}

template <auto ptr, size_t field_no>
IGUANA_INLINE auto build_pb_field(std::string_view name) {
  using owner =
      typename ylt::reflection::member_traits<decltype(ptr)>::owner_type;
  using value_type =
      typename ylt::reflection::member_traits<decltype(ptr)>::value_type;
  size_t offset = member_offset((owner*)nullptr, ptr);
  return iguana::detail::pb_field_t<owner, value_type, field_no>{offset, name};
}

template <auto ptr, size_t field_no, typename... Options>
IGUANA_INLINE auto pb_field_ex(std::string_view name, Options... options) {
  ((void)options, ...);
  using owner =
      typename ylt::reflection::member_traits<decltype(ptr)>::owner_type;
  using value_type = ylt::reflection::remove_cvref_t<
      typename ylt::reflection::member_traits<decltype(ptr)>::value_type>;
  using leaf_type = detail::pb_annotation_leaf_type_t<value_type>;
  using opts = detail::pb_schema_options<Options...>;
  using wire_value_type =
      typename detail::pb_wire_type_selector<opts::zigzag, opts::fixed,
                                             value_type>::type;

  static_assert(detail::is_valid_pb_field_no(field_no),
                "protobuf field number must be in [1, 2^29 - 1] and not in "
                "[19000, 19999]");
  static_assert(!(opts::timestamp && opts::duration),
                "protobuf field can't use both pb_as_timestamp and "
                "pb_as_duration");
  static_assert(!(opts::zigzag && opts::fixed),
                "protobuf field can't use both pb_zigzag and pb_fixed");
  static_assert(!opts::bytes || std::is_same_v<leaf_type, std::string> ||
                    std::is_same_v<leaf_type, std::string_view>,
                "pb_bytes_field member must be std::string, "
                "std::string_view, or optional/vector of that type");
  static_assert(
      !opts::timestamp ||
          std::is_same_v<leaf_type, std::chrono::system_clock::time_point>,
      "pb_timestamp_field member must be "
      "std::chrono::system_clock::time_point, or optional/vector of "
      "that type");
  static_assert(
      !opts::duration || std::is_same_v<leaf_type, std::chrono::nanoseconds>,
      "pb_duration_field member must be std::chrono::nanoseconds, "
      "or optional/vector of that type");
  static_assert(!opts::optional || optional_v<value_type>,
                "pb_optional_field member must be std::optional<T>");
  static_assert(!opts::zigzag || std::is_same_v<leaf_type, int32_t> ||
                    std::is_same_v<leaf_type, int64_t>,
                "pb_zigzag_field member must be int32_t/int64_t, or "
                "optional/vector of that type");
  static_assert(!opts::fixed || std::is_same_v<leaf_type, uint32_t> ||
                    std::is_same_v<leaf_type, uint64_t> ||
                    std::is_same_v<leaf_type, int32_t> ||
                    std::is_same_v<leaf_type, int64_t>,
                "pb_fixed_field member must be 32/64-bit int, or "
                "optional/vector of that type");

  size_t offset = member_offset((owner*)nullptr, ptr);
  return iguana::detail::pb_field_t<
      owner, value_type, field_no, value_type, opts::bytes, opts::timestamp,
      opts::duration, opts::optional, opts::zigzag, opts::fixed,
      wire_value_type, wire_value_type>{offset, name};
}

template <auto ptr, size_t field_no>
IGUANA_INLINE auto pb_field(std::string_view name) {
  return pb_field_ex<ptr, field_no>(name);
}

template <auto ptr, size_t field_no>
IGUANA_INLINE auto pb_bytes_field(std::string_view name) {
  return pb_field_ex<ptr, field_no>(name, pb_bytes);
}

template <auto ptr, size_t field_no>
IGUANA_INLINE auto pb_zigzag_field(std::string_view name) {
  return pb_field_ex<ptr, field_no>(name, pb_zigzag);
}

template <auto ptr, size_t field_no>
IGUANA_INLINE auto pb_fixed_field(std::string_view name) {
  return pb_field_ex<ptr, field_no>(name, pb_fixed);
}

template <auto ptr, size_t field_no>
IGUANA_INLINE auto pb_optional_field(std::string_view name) {
  return pb_field_ex<ptr, field_no>(name, pb_optional);
}

template <auto ptr, size_t field_no>
IGUANA_INLINE auto pb_timestamp_field(std::string_view name) {
  return pb_field_ex<ptr, field_no>(name, pb_as_timestamp);
}

template <auto ptr, size_t field_no>
IGUANA_INLINE auto as_timestamp_field(std::string_view name) {
  return pb_timestamp_field<ptr, field_no>(name);
}

template <auto ptr, size_t field_no>
IGUANA_INLINE auto pb_duration_field(std::string_view name) {
  return pb_field_ex<ptr, field_no>(name, pb_as_duration);
}

template <auto ptr, size_t field_no>
IGUANA_INLINE auto as_duration_field(std::string_view name) {
  return pb_duration_field<ptr, field_no>(name);
}

template <auto ptr>
IGUANA_INLINE auto pb_unknown_fields_field(std::string_view name = "") {
  using owner =
      typename ylt::reflection::member_traits<decltype(ptr)>::owner_type;
  using value_type = ylt::reflection::remove_cvref_t<
      typename ylt::reflection::member_traits<decltype(ptr)>::value_type>;
  static_assert(std::is_same_v<value_type, std::string>,
                "pb_unknown_fields_field member must be std::string");
  size_t offset = member_offset((owner*)nullptr, ptr);
  return iguana::detail::pb_unknown_fields_t<owner, value_type>{offset, name};
}

template <auto ptr, size_t... field_nos>
IGUANA_INLINE auto pb_oneof_field(std::string_view name) {
  using owner =
      typename ylt::reflection::member_traits<decltype(ptr)>::owner_type;
  using value_type =
      typename ylt::reflection::member_traits<decltype(ptr)>::value_type;
  size_t offset = member_offset((owner*)nullptr, ptr);
  return iguana::detail::build_pb_oneof_fields<owner, value_type, field_nos...>(
      offset, name);
}

namespace detail {
template <typename T>
IGUANA_INLINE auto as_pb_member_tuple(T&& value) {
  using U = ylt::reflection::remove_cvref_t<T>;
  if constexpr (is_tuple<U>::value) {
    return std::forward<T>(value);
  }
  else {
    return std::make_tuple(std::forward<T>(value));
  }
}
}  // namespace detail

template <typename... Fields>
IGUANA_INLINE auto pb_members(Fields&&... fields) {
  return std::tuple_cat(
      detail::as_pb_member_tuple(std::forward<Fields>(fields))...);
}
}  // namespace iguana
