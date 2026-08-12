#pragma once
#include "detail/string_resize.hpp"
#include "pb_util.hpp"

namespace iguana {
namespace detail {

template <uint32_t key, typename V, typename Writer>
IGUANA_INLINE void encode_varint_field(V val, Writer& writer) {
  static_assert(std::is_integral_v<V>, "must be integral");
  if constexpr (key != 0) {
    serialize_varint_u32<key>(writer);
  }
  serialize_varint(val, writer);
}

template <uint32_t key, typename V, typename Writer>
IGUANA_INLINE void encode_fixed_field(V val, Writer& writer) {
  if constexpr (key != 0) {
    serialize_varint_u32<key>(writer);
  }
  constexpr size_t size = sizeof(V);
  // TODO: check Stream continuous
  writer.write((const char*)&val, size);
}

template <uint32_t key, bool omit_default_val = true, typename Type,
          typename Writer>
IGUANA_INLINE void to_pb_impl(Type&& t, uint32_t*& sz_ptr, Writer& writer);

template <uint32_t key, bool omit_default_val, typename T, typename Writer>
IGUANA_INLINE void encode_numeric_field(T t, Writer& writer);

template <bool TimestampSchema, uint32_t key, bool omit_default_val = true,
          typename Type, typename Writer>
IGUANA_INLINE void to_pb_well_known_impl(Type&& t, uint32_t*& sz_ptr,
                                         Writer& writer) {
  using T = std::remove_const_t<std::remove_reference_t<Type>>;
  if constexpr (optional_v<T>) {
    if (!t.has_value()) {
      return;
    }
    to_pb_well_known_impl<TimestampSchema, key, omit_default_val>(*t, sz_ptr,
                                                                  writer);
  }
  else if constexpr (is_sequence_container<T>::value) {
    for (auto& item : t) {
      to_pb_well_known_impl<TimestampSchema, key, false>(item, sz_ptr, writer);
    }
  }
  else {
    auto wire_value = make_pb_well_known_value<TimestampSchema>(t);
    to_pb_impl<key, omit_default_val>(wire_value, sz_ptr, writer);
  }
}

template <typename Wire, uint32_t key, bool omit_default_val = true,
          typename Type, typename Writer>
IGUANA_INLINE void to_pb_schema_impl(Type&& t, uint32_t*& sz_ptr,
                                     Writer& writer) {
  using T = ylt::reflection::remove_cvref_t<Type>;
  using W = ylt::reflection::remove_cvref_t<Wire>;
  if constexpr (std::is_same_v<T, W>) {
    to_pb_impl<key, omit_default_val>(std::forward<Type>(t), sz_ptr, writer);
  }
  else if constexpr (optional_v<T>) {
    if (!t.has_value()) {
      return;
    }
    to_pb_schema_impl<typename W::value_type, key, omit_default_val>(*t, sz_ptr,
                                                                     writer);
  }
  else if constexpr (is_sequence_container<T>::value) {
    using wire_item_type = typename W::value_type;
    if constexpr (is_lenprefix_v<wire_item_type>) {
      for (auto& item : t) {
        to_pb_schema_impl<wire_item_type, key, false>(item, sz_ptr, writer);
      }
    }
    else {
      if (t.empty())
        IGUANA_UNLIKELY { return; }
      serialize_varint_u32<key>(writer);
      size_t len = 0;
      for (auto& item : t) {
        auto wire_item = make_pb_wire_scalar<wire_item_type>(item);
        len += str_numeric_size<0, false>(wire_item);
      }
      serialize_varint(len, writer);
      for (auto& item : t) {
        auto wire_item = make_pb_wire_scalar<wire_item_type>(item);
        encode_numeric_field<0, false>(wire_item, writer);
      }
    }
  }
  else {
    auto wire_value = make_pb_wire_scalar<W>(t);
    to_pb_impl<key, omit_default_val>(wire_value, sz_ptr, writer);
  }
}

template <uint32_t key, typename V, typename Writer>
IGUANA_INLINE void encode_pair_value(V&& val, size_t size, uint32_t*& sz_ptr,
                                     Writer& writer) {
  if (size == 0)
    IGUANA_UNLIKELY {
      // map keys can't be omitted even if values are empty
      // TODO: repeated ?
      serialize_varint_u32<key>(writer);
      serialize_varint(0, writer);
    }
  else {
    to_pb_impl<key, false>(val, sz_ptr, writer);
  }
}

template <uint32_t key, bool omit_default_val, typename T, typename Writer>
IGUANA_INLINE void encode_numeric_field(T t, Writer& writer) {
  if constexpr (omit_default_val) {
    if constexpr (is_fixed_v<T> || is_signed_varint_v<T>) {
      if (t.val == 0) {
        return;
      }
    }
    else {
      if (t == static_cast<T>(0))
        IGUANA_UNLIKELY { return; }
    }
  }
  if constexpr (std::is_integral_v<T>) {
    detail::encode_varint_field<key>(t, writer);
  }
  else if constexpr (detail::is_signed_varint_v<T>) {
    detail::encode_varint_field<key>(encode_zigzag(t.val), writer);
  }
  else if constexpr (detail::is_fixed_v<T>) {
    detail::encode_fixed_field<key>(t.val, writer);
  }
  else if constexpr (std::is_same_v<T, double> || std::is_same_v<T, float>) {
    detail::encode_fixed_field<key>(t, writer);
  }
  else if constexpr (std::is_enum_v<T>) {
    using U = std::underlying_type_t<T>;
    detail::encode_varint_field<key>(static_cast<U>(t), writer);
  }
  else {
    static_assert(!sizeof(T), "unsupported type");
  }
}

template <uint32_t field_no, typename Type, typename Writer>
IGUANA_INLINE void to_pb_oneof(Type&& t, uint32_t*& sz_ptr, Writer& writer) {
  using T = std::decay_t<Type>;
  std::visit(
      [&sz_ptr, &writer](auto&& value) IGUANA__INLINE_LAMBDA {
        using raw_value_type = decltype(value);
        using value_type =
            std::remove_const_t<std::remove_reference_t<decltype(value)>>;
        constexpr auto offset =
            get_variant_index<T, value_type, std::variant_size_v<T> - 1>();
        constexpr uint32_t key =
            ((field_no + offset) << 3) |
            static_cast<uint32_t>(get_wire_type<value_type>());
        to_pb_impl<key, false>(std::forward<raw_value_type>(value), sz_ptr,
                               writer);
      },
      std::forward<Type>(t));
}

// omit_default_val = true indicates to omit the default value in searlization
template <uint32_t key, bool omit_default_val, typename Type, typename Writer>
IGUANA_INLINE void to_pb_impl(Type&& t, uint32_t*& sz_ptr, Writer& writer) {
  using T = std::remove_const_t<std::remove_reference_t<Type>>;
  if constexpr (ylt_refletable_v<T> || is_custom_reflection_v<T>) {
    // can't be omitted even if values are empty
    if constexpr (key != 0) {
      auto len = pb_value_size(t, sz_ptr);
      serialize_varint_u32<key>(writer);
      serialize_varint(len, writer);
      if (len == 0)
        IGUANA_UNLIKELY { return; }
    }
    static auto tuple = get_pb_members_tuple(std::forward<Type>(t));
    constexpr size_t SIZE = std::tuple_size_v<std::decay_t<decltype(tuple)>>;
    for_each_n(
        [&t, &sz_ptr, &writer](auto i) IGUANA__INLINE_LAMBDA {
          using field_type =
              std::tuple_element_t<decltype(i)::value,
                                   std::decay_t<decltype(tuple)>>;
          auto value = std::get<decltype(i)::value>(tuple);
          auto& val = value.value(t);

          using U = typename field_type::value_type;
          using wire_type = typename field_type::wire_value_type;
          using sub_type = typename field_type::sub_type;
          if constexpr (field_type::timestamp_schema ||
                        field_type::duration_schema) {
            constexpr uint32_t sub_key =
                (value.field_no << 3) |
                static_cast<uint32_t>(WireType::LengthDelimeted);
            to_pb_well_known_impl<field_type::timestamp_schema, sub_key>(
                val, sz_ptr, writer);
          }
          else if constexpr (field_type::optional_schema) {
            constexpr uint32_t sub_key =
                (value.field_no << 3) |
                static_cast<uint32_t>(get_wire_type<wire_type>());
            to_pb_schema_impl<wire_type, sub_key, false>(val, sz_ptr, writer);
          }
          else if constexpr (variant_v<U>) {
            constexpr auto offset =
                get_variant_index<U, sub_type, std::variant_size_v<U> - 1>();
            if (val.index() == offset) {
              if constexpr (!std::is_same_v<sub_type, std::monostate>) {
                constexpr uint32_t sub_key =
                    (value.field_no << 3) |
                    static_cast<uint32_t>(get_wire_type<sub_type>());
                to_pb_impl<sub_key, false>(std::get<offset>(val), sz_ptr,
                                           writer);
              }
            }
          }
          else {
            constexpr uint32_t sub_key =
                (value.field_no << 3) |
                static_cast<uint32_t>(get_wire_type<wire_type>());
            to_pb_schema_impl<wire_type, sub_key>(val, sz_ptr, writer);
          }
        },
        std::make_index_sequence<SIZE>{});
    write_pb_unknown_fields(t, writer);
  }
  else if constexpr (is_sequence_container<T>::value) {
    // TODO support std::array
    // repeated values can't be omitted even if values are empty
    using item_type = typename T::value_type;
    if constexpr (is_lenprefix_v<item_type>) {
      // non-packed
      for (auto& item : t) {
        to_pb_impl<key, false>(item, sz_ptr, writer);
      }
    }
    else {
      if (t.empty())
        IGUANA_UNLIKELY { return; }
      serialize_varint_u32<key>(writer);
      serialize_varint(pb_value_size(t, sz_ptr), writer);
      for (auto& item : t) {
        encode_numeric_field<false, 0>(item, writer);
      }
    }
  }
  else if constexpr (is_map_container<T>::value) {
    using first_type = typename T::key_type;
    using second_type = typename T::mapped_type;
    constexpr uint32_t key1 =
        (1 << 3) | static_cast<uint32_t>(get_wire_type<first_type>());
    constexpr auto key1_size = variant_uint32_size_constexpr(key1);
    constexpr uint32_t key2 =
        (2 << 3) | static_cast<uint32_t>(get_wire_type<second_type>());
    constexpr auto key2_size = variant_uint32_size_constexpr(key2);

    for (auto& [k, v] : t) {
      serialize_varint_u32<key>(writer);
      // k must be string or numeric
      auto k_val_len = str_numeric_size<0, false>(k);
      auto v_val_len = pb_value_size<false>(v, sz_ptr);
      auto pair_len = key1_size + key2_size + k_val_len + v_val_len;
      if constexpr (is_lenprefix_v<first_type>) {
        pair_len += variant_uint32_size(k_val_len);
      }
      if constexpr (is_lenprefix_v<second_type>) {
        pair_len += variant_uint32_size(v_val_len);
      }
      serialize_varint(pair_len, writer);
      // map k and v can't be omitted even if values are empty
      encode_pair_value<key1>(k, k_val_len, sz_ptr, writer);
      encode_pair_value<key2>(v, v_val_len, sz_ptr, writer);
    }
  }
  else if constexpr (optional_v<T>) {
    if (!t.has_value()) {
      return;
    }
    to_pb_impl<key, omit_default_val>(*t, sz_ptr, writer);
  }
  else if constexpr (std::is_same_v<T, std::string> ||
                     std::is_same_v<T, std::string_view>) {
    if constexpr (omit_default_val) {
      if (t.size() == 0)
        IGUANA_UNLIKELY { return; }
    }
    serialize_varint_u32<key>(writer);
    serialize_varint(t.size(), writer);
    writer.write(t.data(), t.size());
  }
  else {
    encode_numeric_field<key, omit_default_val>(t, writer);
  }
}

#if defined(__clang__) || defined(_MSC_VER) || \
    (defined(__GNUC__) && __GNUC__ > 8)
template <typename T>
IGUANA_INLINE constexpr std::string_view get_type_string() {
  if constexpr (std::is_integral_v<T>) {
    if constexpr (std::is_same_v<T, bool>) {
      return "bool";
    }
    else if constexpr (sizeof(T) <= 4) {
      if constexpr (std::is_unsigned_v<T>) {
        return "uint32";
      }
      else {
        return "int32";
      }
    }
    else {
      if constexpr (std::is_unsigned_v<T>) {
        return "uint64";
      }
      else {
        return "int64";
      }
    }
  }
  else if constexpr (std::is_same_v<T, iguana::sint32_t>) {
    return "sint32";
  }
  else if constexpr (std::is_same_v<T, iguana::sint64_t>) {
    return "sint64";
  }
  else if constexpr (std::is_same_v<T, iguana::fixed32_t>) {
    return "fixed32";
  }
  else if constexpr (std::is_same_v<T, iguana::fixed64_t>) {
    return "fixed64";
  }
  else if constexpr (std::is_same_v<T, iguana::sfixed32_t>) {
    return "sfixed32";
  }
  else if constexpr (std::is_same_v<T, iguana::sfixed64_t>) {
    return "sfixed64";
  }
  else if constexpr (std::is_same_v<T, std::string> ||
                     std::is_same_v<T, std::string_view>) {
    return "string";
  }
  else if constexpr (std::is_floating_point_v<T>) {
    return type_string<T>();
  }
  else {
    constexpr auto str_type_name = type_string<T>();
    constexpr size_t pos = str_type_name.rfind("::");
    if constexpr (pos != std::string_view::npos) {
      constexpr size_t pos = str_type_name.rfind("::") + 2;
      if constexpr (detail::is_signed_varint_v<T> || detail::is_fixed_v<T>) {
        return str_type_name.substr(pos, str_type_name.size() - pos - 2);
      }
      else {
        return str_type_name.substr(pos);
      }
    }
    else {
      return str_type_name;
    }
  }
}

template <typename T, typename Stream>
IGUANA_INLINE void numeric_to_proto(Stream& out, std::string_view field_name,
                                    uint32_t field_no) {
  constexpr auto name = get_type_string<T>();
  out.append(name).append(" ");
  out.append(field_name)
      .append(" = ")
      .append(std::to_string(field_no))
      .append(";\n");
}

template <size_t space_count = 2, typename Stream>
IGUANA_INLINE void build_proto_field(Stream& out, std::string_view str_type,
                                     std::string_view field_name,
                                     uint32_t field_no) {
  for (size_t i = 0; i < space_count; i++) {
    out.append(" ");
  }

  if (!str_type.empty()) {
    out.append(str_type);
  }

  out.append(" ")
      .append(field_name)
      .append(" = ")
      .append(std::to_string(field_no))
      .append(";\n");
}

template <typename T, typename Map>
IGUANA_INLINE void build_sub_proto(Map& map, std::string_view str_type,
                                   std::string& sub_str);

template <typename Type, typename Stream>
IGUANA_INLINE void to_proto_impl(
    Stream& out, std::unordered_map<std::string_view, std::string>& map,
    std::string_view field_name = "", uint32_t field_no = 0) {
  std::string sub_str;
  using T = std::remove_const_t<std::remove_reference_t<Type>>;
  if constexpr (ylt_refletable_v<T> || is_custom_reflection_v<T>) {
    constexpr auto name = type_string<T>();
    out.append("message ").append(name).append(" {\n");
    static T t;
    static auto tuple = get_pb_members_tuple(t);
    constexpr size_t SIZE = std::tuple_size_v<std::decay_t<decltype(tuple)>>;

    for_each_n(
        [&out, &sub_str, &map](auto i) mutable {
          using field_type =
              std::tuple_element_t<decltype(i)::value,
                                   std::decay_t<decltype(tuple)>>;
          auto value = std::get<decltype(i)::value>(tuple);

          using U = typename field_type::value_type;
          using WireU = typename field_type::wire_value_type;
          using sub_type = typename field_type::sub_type;
          if constexpr (field_type::bytes_schema) {
            if constexpr (is_sequence_container<U>::value) {
              build_proto_field(
                  out, "repeated bytes",
                  {value.field_name.data(), value.field_name.size()},
                  value.field_no);
            }
            else if constexpr (optional_v<U>) {
              out.append("  optional");
              build_proto_field(
                  out, "bytes",
                  {value.field_name.data(), value.field_name.size()},
                  value.field_no);
            }
            else {
              build_proto_field(
                  out, "bytes ",
                  {value.field_name.data(), value.field_name.size()},
                  value.field_no);
            }
          }
          else if constexpr (field_type::timestamp_schema) {
            if constexpr (is_sequence_container<U>::value) {
              build_proto_field(
                  out, "repeated google.protobuf.Timestamp",
                  {value.field_name.data(), value.field_name.size()},
                  value.field_no);
            }
            else {
              build_proto_field(
                  out, "google.protobuf.Timestamp",
                  {value.field_name.data(), value.field_name.size()},
                  value.field_no);
            }
          }
          else if constexpr (field_type::duration_schema) {
            if constexpr (is_sequence_container<U>::value) {
              build_proto_field(
                  out, "repeated google.protobuf.Duration",
                  {value.field_name.data(), value.field_name.size()},
                  value.field_no);
            }
            else {
              build_proto_field(
                  out, "google.protobuf.Duration",
                  {value.field_name.data(), value.field_name.size()},
                  value.field_no);
            }
          }
          else if constexpr (field_type::optional_schema) {
            static_assert(optional_v<U>,
                          "pb_optional member must be std::optional<T>");
            out.append("  optional");
            to_proto_impl<typename WireU::value_type>(
                out, map, {value.field_name.data(), value.field_name.size()},
                value.field_no);
          }
          else if constexpr (ylt_refletable_v<U>) {
            constexpr auto str_type = get_type_string<U>();
            build_proto_field(
                out, str_type,
                {value.field_name.data(), value.field_name.size()},
                value.field_no);

            build_sub_proto<U>(map, str_type, sub_str);
          }
          else if constexpr (variant_v<U>) {
            constexpr size_t var_size = std::variant_size_v<U>;
            constexpr size_t first_case = pb_variant_first_case_index<U>();

            constexpr auto offset =
                get_variant_index<U, sub_type, var_size - 1>();

            if (offset == first_case) {
              out.append("  oneof ");
              out.append(value.field_name.data(), value.field_name.size())
                  .append(" {\n");
            }

            if constexpr (!std::is_same_v<sub_type, std::monostate>) {
              constexpr auto str_type = get_type_string<sub_type>();
              std::string field_name = " one_of_";
              field_name.append(str_type);

              out.append("  ");
              build_proto_field(out, str_type, field_name, value.field_no);

              if constexpr (ylt_refletable_v<sub_type>) {
                build_sub_proto<sub_type>(map, str_type, sub_str);
              }
            }

            if (offset == var_size - 1) {
              out.append("  }\n");
            }
          }
          else {
            to_proto_impl<WireU>(
                out, map, {value.field_name.data(), value.field_name.size()},
                value.field_no);
          }
        },
        std::make_index_sequence<SIZE>{});
    out.append("}\r\n\r\n");
  }
  else if constexpr (is_sequence_container<T>::value) {
    out.append("  repeated");
    using item_type = typename T::value_type;

    if constexpr (is_lenprefix_v<item_type>) {
      // non-packed
      if constexpr (ylt_refletable_v<item_type>) {
        constexpr auto str_type = get_type_string<item_type>();
        build_proto_field(out, str_type, field_name, field_no);

        build_sub_proto<item_type>(map, str_type, sub_str);
      }
      else {
        to_proto_impl<item_type>(out, map, field_name, field_no);
      }
    }
    else {
      out.append("  ");
      numeric_to_proto<item_type>(out, field_name, field_no);
    }
  }
  else if constexpr (is_map_container<T>::value) {
    out.append("  map<");
    using first_type = typename T::key_type;
    using second_type = typename T::mapped_type;

    constexpr auto str_first = get_type_string<first_type>();
    constexpr auto str_second = get_type_string<second_type>();
    out.append(str_first).append(", ").append(str_second).append(">");

    build_proto_field<1>(out, "", field_name, field_no);

    if constexpr (ylt_refletable_v<second_type>) {
      constexpr auto str_type = get_type_string<second_type>();
      build_sub_proto<second_type>(map, str_type, sub_str);
    }
  }
  else if constexpr (optional_v<T>) {
    to_proto_impl<typename T::value_type>(
        out, map, {field_name.data(), field_name.size()}, field_no);
  }
  else if constexpr (std::is_same_v<T, std::string> ||
                     std::is_same_v<T, std::string_view>) {
    build_proto_field(out, "string ", field_name, field_no);
  }
  else if constexpr (enum_v<T>) {
    constexpr auto str_type = get_type_string<T>();
    static constexpr auto enum_to_str = get_enum_map<false, std::decay_t<T>>();
    if constexpr (bool_v<decltype(enum_to_str)>) {
      build_proto_field(out, "int32", field_name, field_no);
    }
    else {
      static_assert(enum_to_str.size() > 0, "empty enum not allowed");
      static_assert((int)(enum_to_str.begin()->first) == 0,
                    "the first enum value must be zero in proto3");
      build_proto_field(out, str_type, field_name, field_no);
      if (map.find(str_type) == map.end()) {
        sub_str.append("enum ").append(str_type).append(" {\n");
        for (auto& [k, field_name] : enum_to_str) {
          std::string_view name{field_name.data(), field_name.size()};
          size_t pos = name.rfind("::");
          if (pos != std::string_view::npos) {
            name = name.substr(pos + 2);
          }
          sub_str.append("  ")
              .append(name)
              .append(" = ")
              .append(std::to_string(static_cast<std::underlying_type_t<T>>(k)))
              .append(";\n");
        }
        sub_str.append("}\r\n\r\n");
        map.emplace(str_type, std::move(sub_str));
      }
    }
  }
  else {
    out.append("  ");
    numeric_to_proto<Type>(out, field_name, field_no);
  }
}

template <typename T, typename Map>
IGUANA_INLINE void build_sub_proto(Map& map, std::string_view str_type,
                                   std::string& sub_str) {
  if (map.find(str_type) == map.end()) {
    to_proto_impl<T>(sub_str, map);
    map.emplace(str_type, std::move(sub_str));
  }
}

template <typename T>
constexpr bool proto_needs_timestamp_import();

template <typename T>
constexpr bool proto_needs_duration_import();

template <typename T>
constexpr bool proto_value_needs_timestamp_import();

template <typename T>
constexpr bool proto_value_needs_duration_import();

template <typename Variant, size_t... I>
constexpr bool proto_variant_needs_timestamp_import(std::index_sequence<I...>) {
  return (proto_value_needs_timestamp_import<
              std::variant_alternative_t<I, Variant>>() ||
          ...);
}

template <typename Variant, size_t... I>
constexpr bool proto_variant_needs_duration_import(std::index_sequence<I...>) {
  return (proto_value_needs_duration_import<
              std::variant_alternative_t<I, Variant>>() ||
          ...);
}

template <typename T>
constexpr bool proto_value_needs_timestamp_import() {
  using U = std::remove_cvref_t<T>;
  if constexpr (std::is_same_v<U, std::monostate> ||
                pb_well_known_chrono_v<U>) {
    return false;
  }
  else if constexpr (optional_v<U>) {
    return proto_value_needs_timestamp_import<typename U::value_type>();
  }
  else if constexpr (is_sequence_container<U>::value) {
    return proto_value_needs_timestamp_import<typename U::value_type>();
  }
  else if constexpr (is_map_container<U>::value) {
    return proto_value_needs_timestamp_import<typename U::mapped_type>();
  }
  else if constexpr (variant_v<U>) {
    return proto_variant_needs_timestamp_import<U>(
        std::make_index_sequence<std::variant_size_v<U>>{});
  }
  else if constexpr (ylt_refletable_v<U> || is_custom_reflection_v<U>) {
    return proto_needs_timestamp_import<U>();
  }
  else {
    return false;
  }
}

template <typename T>
constexpr bool proto_value_needs_duration_import() {
  using U = std::remove_cvref_t<T>;
  if constexpr (std::is_same_v<U, std::monostate> ||
                pb_well_known_chrono_v<U>) {
    return false;
  }
  else if constexpr (optional_v<U>) {
    return proto_value_needs_duration_import<typename U::value_type>();
  }
  else if constexpr (is_sequence_container<U>::value) {
    return proto_value_needs_duration_import<typename U::value_type>();
  }
  else if constexpr (is_map_container<U>::value) {
    return proto_value_needs_duration_import<typename U::mapped_type>();
  }
  else if constexpr (variant_v<U>) {
    return proto_variant_needs_duration_import<U>(
        std::make_index_sequence<std::variant_size_v<U>>{});
  }
  else if constexpr (ylt_refletable_v<U> || is_custom_reflection_v<U>) {
    return proto_needs_duration_import<U>();
  }
  else {
    return false;
  }
}

template <typename Tuple, size_t... I>
constexpr bool proto_tuple_needs_timestamp_import(std::index_sequence<I...>) {
  return ((std::tuple_element_t<I, Tuple>::timestamp_schema ||
           proto_value_needs_timestamp_import<
               typename std::tuple_element_t<I, Tuple>::value_type>()) ||
          ...);
}

template <typename Tuple, size_t... I>
constexpr bool proto_tuple_needs_duration_import(std::index_sequence<I...>) {
  return ((std::tuple_element_t<I, Tuple>::duration_schema ||
           proto_value_needs_duration_import<
               typename std::tuple_element_t<I, Tuple>::value_type>()) ||
          ...);
}

template <typename T>
constexpr bool proto_needs_timestamp_import() {
  if constexpr (ylt_refletable_v<T> || is_custom_reflection_v<T>) {
    using Tuple =
        std::decay_t<decltype(get_pb_members_tuple(std::declval<T&>()))>;
    return proto_tuple_needs_timestamp_import<Tuple>(
        std::make_index_sequence<std::tuple_size_v<Tuple>>{});
  }
  else {
    return false;
  }
}

template <typename T>
constexpr bool proto_needs_duration_import() {
  if constexpr (ylt_refletable_v<T> || is_custom_reflection_v<T>) {
    using Tuple =
        std::decay_t<decltype(get_pb_members_tuple(std::declval<T&>()))>;
    return proto_tuple_needs_duration_import<Tuple>(
        std::make_index_sequence<std::tuple_size_v<Tuple>>{});
  }
  else {
    return false;
  }
}
#endif
}  // namespace detail

template <
    typename T, typename Stream,
    std::enable_if_t<ylt_refletable_v<T> || detail::is_custom_reflection_v<T>,
                     int> = 0>
IGUANA_INLINE void to_pb(T const& t, Stream& out) {
  std::vector<uint32_t> size_arr;
  auto byte_len = detail::pb_key_value_size<0>(t, size_arr);
  auto sz_ptr = size_arr.empty() ? nullptr : &size_arr[0];

  if constexpr (is_resizable_char_container_v<Stream>) {
    detail::resize(out, byte_len);
    memory_writer writer{out.data()};
    detail::to_pb_impl<0>(t, sz_ptr, writer);
  }
  else if constexpr (char_writer<Stream>) {
    detail::to_pb_impl<0>(t, sz_ptr, out);
  }
  else {
    static_assert(!sizeof(Stream), "Invalid stream type");
  }
}

#if defined(__clang__) || defined(_MSC_VER) || \
    (defined(__GNUC__) && __GNUC__ > 8)
template <typename T, bool gen_header = true, typename Stream>
IGUANA_INLINE void to_proto(Stream& out, std::string_view ns = "") {
  if (gen_header) {
    constexpr std::string_view crlf = "\r\n\r\n";
    out.append(R"(syntax = "proto3";)").append(crlf);
    if constexpr (detail::proto_needs_timestamp_import<T>()) {
      out.append(R"(import "google/protobuf/timestamp.proto";)").append(crlf);
    }
    if constexpr (detail::proto_needs_duration_import<T>()) {
      out.append(R"(import "google/protobuf/duration.proto";)").append(crlf);
    }
    if (!ns.empty()) {
      out.append("package ").append(ns).append(";").append(crlf);
    }

    out.append(R"(option optimize_for = SPEED;)").append(crlf);
    out.append(R"(option cc_enable_arenas = true;)").append(crlf);
  }

  std::unordered_map<std::string_view, std::string> map;
  detail::to_proto_impl<T>(out, map);
  for (auto& [k, s] : map) {
    out.append(s);
  }
}

template <typename T, bool gen_header = true, typename Stream>
IGUANA_INLINE void to_proto_file(Stream& stream, std::string_view ns = "") {
  if (!stream.is_open()) {
    return;
  }
  std::string out;
  to_proto<T, gen_header>(out, ns);
  stream.write(out.data(), out.size());
}
#endif

template <typename T, typename Stream>
IGUANA_INLINE void to_pb_adl(iguana_adl_t* p, T const& t, Stream& out) {
  to_pb(t, out);
}

}  // namespace iguana
