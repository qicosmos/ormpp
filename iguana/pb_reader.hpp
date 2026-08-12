#pragma once
#include "detail/string_resize.hpp"
#include "pb_util.hpp"

namespace iguana {
template <typename T>
IGUANA_INLINE void from_pb(T& t, std::string_view pb_str);
namespace detail {

template <typename T>
IGUANA_INLINE void from_pb_impl(T& val, std::string_view& pb_str,
                                uint32_t field_no = 0);

inline void skip_unknown_field(std::string_view& pb_str, WireType wire_type,
                               uint32_t field_number = 0);

template <typename T>
IGUANA_INLINE void decode_pair_value(T& val, std::string_view& pb_str) {
  auto key = detail::decode_key(pb_str);
  pb_str = pb_str.substr(key.tag_size);
  WireType wire_type = key.wire_type;
  if (wire_type != detail::get_wire_type<std::remove_reference_t<T>>()) {
    return;
  }
  from_pb_impl(val, pb_str);
}

template <typename T>
IGUANA_INLINE void from_pb_impl(T& val, std::string_view& pb_str,
                                uint32_t field_no) {
  size_t pos = 0;
  if constexpr (ylt_refletable_v<T>) {
    const size_t size = detail::decode_length_delimited(
        pb_str, "Invalid message value: too few bytes.");
    if (size == 0) {
      return;
    }
    from_pb(val, pb_str.substr(0, size));
    pb_str = pb_str.substr(size);
  }
  else if constexpr (is_sequence_container<T>::value) {
    using item_type = typename T::value_type;
    if constexpr (is_lenprefix_v<item_type>) {
      // item_type non-packed
      while (!pb_str.empty()) {
        item_type item{};
        from_pb_impl(item, pb_str);
        val.push_back(std::move(item));
        if (pb_str.empty()) {
          break;
        }
        auto key = detail::decode_key(pb_str);
        if (key.field_number != field_no ||
            key.wire_type != detail::get_wire_type<item_type>()) {
          break;
        }
        else {
          pb_str = pb_str.substr(key.tag_size);
        }
      }
    }
    else {
      // item_type packed
      const size_t size = detail::decode_length_delimited(
          pb_str, "Invalid packed repeated value: too few bytes.");
      std::string_view packed = pb_str.substr(0, size);
      pb_str = pb_str.substr(size);
      if constexpr (is_fixed_v<item_type>) {
        if (size % sizeof(item_type) != 0)
          IGUANA_UNLIKELY {
            throw std::invalid_argument(
                "Invalid packed fixed repeated value: bad length.");
          }
        size_t num = size / sizeof(item_type);
        size_t old_size = val.size();
        detail::resize(val, old_size + num);
        std::memcpy(val.data() + old_size, packed.data(), size);
      }
      else {
        while (!packed.empty()) {
          item_type item;
          from_pb_impl(item, packed);
          val.push_back(std::move(item));
        }
      }
    }
  }
  else if constexpr (is_map_container<T>::value) {
    using key_type = typename T::key_type;
    using mapped_type = typename T::mapped_type;
    while (!pb_str.empty()) {
      const size_t size = detail::decode_length_delimited(
          pb_str, "Invalid map entry value: too few bytes.");
      std::string_view entry = pb_str.substr(0, size);
      pb_str = pb_str.substr(size);

      key_type map_key{};
      mapped_type mapped{};
      while (!entry.empty()) {
        auto entry_key = detail::decode_key(entry);
        WireType entry_wire_type = entry_key.wire_type;
        uint32_t entry_field_number = entry_key.field_number;
        entry = entry.substr(entry_key.tag_size);

        if (entry_field_number == 1) {
          if (entry_wire_type == detail::get_wire_type<key_type>()) {
            from_pb_impl(map_key, entry, 1);
          }
          else {
            skip_unknown_field(entry, entry_wire_type, entry_field_number);
          }
        }
        else if (entry_field_number == 2) {
          if (entry_wire_type == detail::get_wire_type<mapped_type>()) {
            from_pb_impl(mapped, entry, 2);
          }
          else {
            skip_unknown_field(entry, entry_wire_type, entry_field_number);
          }
        }
        else {
          skip_unknown_field(entry, entry_wire_type, entry_field_number);
        }
      }
      val.insert_or_assign(std::move(map_key), std::move(mapped));

      if (pb_str.empty()) {
        break;
      }
      auto key = detail::decode_key(pb_str);
      if (key.field_number != field_no ||
          key.wire_type != WireType::LengthDelimeted) {
        break;
      }
      pb_str = pb_str.substr(key.tag_size);
    }
  }
  else if constexpr (std::is_integral_v<T>) {
    val = static_cast<T>(detail::decode_varint(pb_str, pos));
    pb_str = pb_str.substr(pos);
  }
  else if constexpr (detail::is_signed_varint_v<T>) {
    constexpr size_t len = sizeof(typename T::value_type);
    uint64_t temp = detail::decode_varint(pb_str, pos);
    if constexpr (len == 8) {
      val.val = detail::decode_zigzag(temp);
    }
    else {
      val.val = detail::decode_zigzag(static_cast<uint32_t>(temp));
    }
    pb_str = pb_str.substr(pos);
  }
  else if constexpr (detail::is_fixed_v<T>) {
    constexpr size_t size = sizeof(typename T::value_type);
    if (pb_str.size() < size)
      IGUANA_UNLIKELY {
        throw std::invalid_argument("Invalid fixed int value: too few bytes.");
      }
    memcpy(&(val.val), pb_str.data(), size);
    pb_str = pb_str.substr(size);
  }
  else if constexpr (std::is_same_v<T, double> || std::is_same_v<T, float>) {
    constexpr size_t size = sizeof(T);
    if (pb_str.size() < size)
      IGUANA_UNLIKELY {
        throw std::invalid_argument("Invalid fixed int value: too few bytes.");
      }
    memcpy(&(val), pb_str.data(), size);
    pb_str = pb_str.substr(size);
  }
  else if constexpr (std::is_same_v<T, std::string> ||
                     std::is_same_v<T, std::string_view>) {
    const size_t size = detail::decode_length_delimited(
        pb_str, "Invalid string value: too few bytes.");
    if constexpr (std::is_same_v<T, std::string_view>) {
      val = std::string_view(pb_str.data(), size);
    }
    else {
      detail::resize(val, size);
      memcpy(val.data(), pb_str.data(), size);
    }
    pb_str = pb_str.substr(size);
  }
  else if constexpr (std::is_enum_v<T>) {
    using U = std::underlying_type_t<T>;
    U value{};
    from_pb_impl(value, pb_str);
    val = static_cast<T>(value);
  }
  else if constexpr (optional_v<T>) {
    if (!val.has_value()) {
      val.emplace();
    }
    from_pb_impl(*val, pb_str, field_no);
  }
  else {
    static_assert(!sizeof(T), "err");
  }
}

template <typename T, typename Field>
IGUANA_INLINE void parse_oneof(T& t, const Field& f, std::string_view& pb_str) {
  using item_type = typename std::decay_t<Field>::sub_type;
  from_pb_impl(t.template emplace<item_type>(), pb_str, f.field_no);
}

template <typename T>
constexpr bool is_unpacked_repeated_type() {
  using value_type = std::remove_const_t<std::remove_reference_t<T>>;
  if constexpr (is_sequence_container<value_type>::value) {
    using item_type = typename value_type::value_type;
    return !is_lenprefix_v<item_type>;
  }
  else {
    return false;
  }
}

template <typename T>
constexpr bool is_unpacked_repeated_wire_type(WireType wire_type) {
  using value_type = std::remove_const_t<std::remove_reference_t<T>>;
  if constexpr (is_unpacked_repeated_type<value_type>()) {
    using item_type = typename value_type::value_type;
    return wire_type == detail::get_wire_type<item_type>();
  }
  else {
    return false;
  }
}

template <typename T>
IGUANA_INLINE void from_pb_unpacked_repeated_item(T& val,
                                                  std::string_view& pb_str) {
  using value_type = std::remove_const_t<std::remove_reference_t<T>>;
  using item_type = typename value_type::value_type;
  item_type item{};
  from_pb_impl(item, pb_str);
  val.push_back(std::move(item));
}

template <bool TimestampSchema, typename T>
IGUANA_INLINE auto from_pb_well_known_value(const T* current,
                                            std::string_view& pb_str,
                                            uint32_t field_no) {
  using value_type = std::remove_const_t<std::remove_reference_t<T>>;
  if constexpr (TimestampSchema) {
    iguana::pb_timestamp wire_value{};
    if (current != nullptr) {
      wire_value = iguana::pb_timestamp{*current};
    }
    from_pb_impl(wire_value, pb_str, field_no);
    return static_cast<value_type>(wire_value);
  }
  else {
    iguana::pb_duration wire_value{};
    if (current != nullptr) {
      wire_value = iguana::pb_duration{*current};
    }
    from_pb_impl(wire_value, pb_str, field_no);
    return static_cast<value_type>(wire_value);
  }
}

template <bool TimestampSchema, typename T>
IGUANA_INLINE void from_pb_well_known_impl(T& val, std::string_view& pb_str,
                                           uint32_t field_no) {
  using value_type = std::remove_const_t<std::remove_reference_t<T>>;
  if constexpr (optional_v<value_type>) {
    using item_type = typename value_type::value_type;
    const item_type* current = val.has_value() ? &*val : nullptr;
    val = from_pb_well_known_value<TimestampSchema, item_type>(current, pb_str,
                                                               field_no);
  }
  else if constexpr (is_sequence_container<value_type>::value) {
    using item_type = typename value_type::value_type;
    val.push_back(from_pb_well_known_value<TimestampSchema, item_type>(
        nullptr, pb_str, field_no));
  }
  else {
    val = from_pb_well_known_value<TimestampSchema, value_type>(&val, pb_str,
                                                                field_no);
  }
}

template <typename Wire, typename T>
IGUANA_INLINE void from_pb_schema_impl(T& val, std::string_view& pb_str,
                                       uint32_t field_no) {
  using value_type = ylt::reflection::remove_cvref_t<T>;
  using wire_type = ylt::reflection::remove_cvref_t<Wire>;
  if constexpr (std::is_same_v<value_type, wire_type>) {
    from_pb_impl(val, pb_str, field_no);
  }
  else if constexpr (is_sequence_container<value_type>::value) {
    using wire_item_type = typename wire_type::value_type;
    if constexpr (is_lenprefix_v<wire_item_type>) {
      wire_type wire_value{};
      from_pb_impl(wire_value, pb_str, field_no);
      assign_pb_wire_value(val, std::move(wire_value));
    }
    else {
      const size_t size = decode_length_delimited(
          pb_str, "Invalid packed repeated value: too few bytes.");
      std::string_view packed = pb_str.substr(0, size);
      pb_str = pb_str.substr(size);
      while (!packed.empty()) {
        wire_item_type wire_item{};
        from_pb_impl(wire_item, packed);
        typename value_type::value_type item{};
        assign_pb_wire_value(item, std::move(wire_item));
        val.push_back(std::move(item));
      }
    }
  }
  else {
    wire_type wire_value{};
    from_pb_impl(wire_value, pb_str, field_no);
    assign_pb_wire_value(val, std::move(wire_value));
  }
}

// pb_str must already point past the field tag varint.
inline void skip_unknown_field(std::string_view& pb_str, WireType wire_type,
                               uint32_t field_number) {
  switch (wire_type) {
    case WireType::Varint: {
      if (pb_str.empty())
        throw std::invalid_argument("unknown Varint field: too few bytes");
      size_t skip_pos = 0;
      decode_varint(pb_str, skip_pos);
      pb_str = pb_str.substr(skip_pos);
      break;
    }
    case WireType::Fixed64:
      if (pb_str.size() < 8)
        throw std::invalid_argument("unknown Fixed64 field: too few bytes");
      pb_str = pb_str.substr(8);
      break;
    case WireType::LengthDelimeted: {
      const size_t len = decode_length_delimited(
          pb_str, "unknown LengthDelimited field: too few bytes");
      pb_str = pb_str.substr(len);
      break;
    }
    case WireType::Fixed32:
      if (pb_str.size() < 4)
        throw std::invalid_argument("unknown Fixed32 field: too few bytes");
      pb_str = pb_str.substr(4);
      break;
    case WireType::StartGroup: {
      pb_recursion_guard recursion_guard;
      while (true) {
        if (pb_str.empty())
          throw std::invalid_argument("unknown group field: missing end tag");
        auto key = detail::decode_key(pb_str, true);
        pb_str = pb_str.substr(key.tag_size);
        WireType child_wire_type = key.wire_type;
        uint32_t child_field_number = key.field_number;
        if (child_wire_type == WireType::EndGroup) {
          if (child_field_number != field_number) {
            throw std::invalid_argument(
                "unknown group field: mismatched end tag");
          }
          return;
        }
        skip_unknown_field(pb_str, child_wire_type, child_field_number);
      }
    }
    case WireType::EndGroup:
      throw std::runtime_error("unexpected end group in unknown field");
    default:
      throw std::runtime_error("unknown wire type in unknown field");
  }
}
}  // namespace detail

template <typename T>
IGUANA_INLINE void from_pb(T& t, std::string_view pb_str) {
  if (pb_str.empty())
    IGUANA_UNLIKELY { return; }
  detail::pb_recursion_guard recursion_guard;
  auto key = detail::decode_key(pb_str);
  size_t pos = key.tag_size;
  WireType wire_type = key.wire_type;
  uint32_t field_number = key.field_number;
#ifdef SEQUENTIAL_PARSE
  static auto tp = detail::get_pb_members_tuple(t);
  constexpr size_t SIZE = std::tuple_size_v<std::decay_t<decltype(tp)>>;
  bool parse_done = false;
  auto ptr = &t;
  detail::for_each_n(
      [&, ptr](auto i) IGUANA__INLINE_LAMBDA {
        auto val = std::get<decltype(i)::value>(tp);
        using sub_type = typename std::decay_t<decltype(val)>::sub_type;
        using value_type = typename std::decay_t<decltype(val)>::value_type;
        using wire_value_type =
            typename std::decay_t<decltype(val)>::wire_value_type;
        using wire_sub_type =
            typename std::decay_t<decltype(val)>::wire_sub_type;
        constexpr bool is_variant_v = variant_v<value_type>;
        // sub_type is the element type when value_type is the variant type;
        // otherwise, they are the same.
        if (parse_done || field_number != val.field_no) {
          return;
        }
        pb_str = pb_str.substr(pos);
        if constexpr (std::decay_t<decltype(val)>::timestamp_schema ||
                      std::decay_t<decltype(val)>::duration_schema) {
          if (wire_type != WireType::LengthDelimeted) {
            throw std::runtime_error("unmatched wire_type");
          }
        }
        else if constexpr (std::is_same_v<sub_type, std::monostate>) {
          throw std::runtime_error("oneof monostate has no wire type");
        }
        else if constexpr (detail::is_unpacked_repeated_type<
                               wire_value_type>()) {
          if (wire_type != WireType::LengthDelimeted &&
              !detail::is_unpacked_repeated_wire_type<wire_value_type>(
                  wire_type)) {
            throw std::runtime_error("unmatched wire_type");
          }
        }
        else if (wire_type != detail::get_wire_type<wire_sub_type>())
          IGUANA_UNLIKELY { throw std::runtime_error("unmatched wire_type"); }

        auto member_ptr = (value_type*)((char*)(ptr) + val.offset);
        if constexpr (std::decay_t<decltype(val)>::timestamp_schema ||
                      std::decay_t<decltype(val)>::duration_schema) {
          detail::from_pb_well_known_impl<
              std::decay_t<decltype(val)>::timestamp_schema>(
              *member_ptr, pb_str, val.field_no);
        }
        else if constexpr (detail::is_unpacked_repeated_type<
                               wire_value_type>()) {
          if (wire_type == WireType::LengthDelimeted) {
            detail::from_pb_schema_impl<wire_value_type>(*member_ptr, pb_str,
                                                         val.field_no);
          }
          else {
            typename wire_value_type::value_type wire_item{};
            detail::from_pb_impl(wire_item, pb_str);
            typename value_type::value_type item{};
            detail::assign_pb_wire_value(item, std::move(wire_item));
            member_ptr->push_back(std::move(item));
          }
        }
        else if constexpr (is_variant_v) {
          detail::parse_oneof(*member_ptr, val, pb_str);
        }
        else {
          detail::from_pb_schema_impl<wire_value_type>(*member_ptr, pb_str,
                                                       val.field_no);
        }
        if (pb_str.empty()) {
          parse_done = true;
          return;
        }
        key = detail::decode_key(pb_str);
        pos = key.tag_size;
        wire_type = key.wire_type;
        field_number = key.field_number;
      },
      std::make_index_sequence<SIZE>{});
  if (parse_done)
    IGUANA_LIKELY { return; }
#endif
  static auto map = detail::get_members(t);
  while (true) {
    const char* field_start = pb_str.data();
    pb_str = pb_str.substr(pos);
    auto it = map.find(field_number);
    if (it == map.end()) {
      // Unknown field: skip according to wire type (proto3 forward compat)
      detail::skip_unknown_field(pb_str, wire_type, field_number);
      detail::append_pb_unknown_field(
          t, field_start, static_cast<size_t>(pb_str.data() - field_start));
    }
    else {
      auto& member = it->second;
      std::visit(
          [&t, &pb_str, wire_type](auto& val) {
            using sub_type = typename std::decay_t<decltype(val)>::sub_type;
            using value_type = typename std::decay_t<decltype(val)>::value_type;
            using wire_value_type =
                typename std::decay_t<decltype(val)>::wire_value_type;
            using wire_sub_type =
                typename std::decay_t<decltype(val)>::wire_sub_type;
            if constexpr (std::decay_t<decltype(val)>::timestamp_schema ||
                          std::decay_t<decltype(val)>::duration_schema) {
              if (wire_type != WireType::LengthDelimeted) {
                throw std::runtime_error("unmatched wire_type");
              }
            }
            else if constexpr (std::is_same_v<sub_type, std::monostate>) {
              throw std::runtime_error("oneof monostate has no wire type");
            }
            else if constexpr (detail::is_unpacked_repeated_type<
                                   wire_value_type>()) {
              if (wire_type != WireType::LengthDelimeted &&
                  !detail::is_unpacked_repeated_wire_type<wire_value_type>(
                      wire_type)) {
                throw std::runtime_error("unmatched wire_type");
              }
            }
            else if (wire_type != detail::get_wire_type<wire_sub_type>()) {
              throw std::runtime_error("unmatched wire_type");
            }
            if constexpr (std::decay_t<decltype(val)>::timestamp_schema ||
                          std::decay_t<decltype(val)>::duration_schema) {
              detail::from_pb_well_known_impl<
                  std::decay_t<decltype(val)>::timestamp_schema>(
                  val.value(t), pb_str, val.field_no);
            }
            else if constexpr (detail::is_unpacked_repeated_type<
                                   wire_value_type>()) {
              if (wire_type == WireType::LengthDelimeted) {
                detail::from_pb_schema_impl<wire_value_type>(
                    val.value(t), pb_str, val.field_no);
              }
              else {
                typename wire_value_type::value_type wire_item{};
                detail::from_pb_impl(wire_item, pb_str);
                typename value_type::value_type item{};
                detail::assign_pb_wire_value(item, std::move(wire_item));
                val.value(t).push_back(std::move(item));
              }
            }
            else if constexpr (variant_v<value_type>) {
              detail::parse_oneof(val.value(t), val, pb_str);
            }
            else {
              detail::from_pb_schema_impl<wire_value_type>(val.value(t), pb_str,
                                                           val.field_no);
            }
          },
          member);
    }
    if (!pb_str.empty())
      IGUANA_LIKELY {
        key = detail::decode_key(pb_str);
        pos = key.tag_size;
        wire_type = key.wire_type;
        field_number = key.field_number;
      }
    else {
      return;
    }
  }
}

template <typename T>
IGUANA_INLINE void from_pb_adl(iguana_adl_t* p, T& t, std::string_view pb_str) {
  iguana::from_pb(t, pb_str);
}
}  // namespace iguana
