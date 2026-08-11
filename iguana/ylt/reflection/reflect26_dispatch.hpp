#pragma once
#ifdef YLT_USE_CXX26_REFLECTION
#include <meta>
#include <string_view>
#include <type_traits>
#include <utility>

#include "member_names.hpp"

namespace ylt::reflection::reflect26 {

template <typename Func, typename Field>
void invoke_dispatch(Func& func, Field& field, std::string_view name,
                     std::size_t index) {
  if constexpr (std::is_invocable_v<Func&, Field&, std::string_view,
                                    std::size_t>) {
    func(field, name, index);
  }
  else if constexpr (std::is_invocable_v<Func&, Field&, std::string_view>) {
    func(field, name);
  }
  else if constexpr (std::is_invocable_v<Func&, Field&>) {
    func(field);
  }
  else {
    static_assert(sizeof(Func) < 0,
                  "invalid arguments, full arguments: [field_value&, "
                  "std::string_view, size_t], at least has field_value and "
                  "make sure keep the order of arguments");
  }
}

template <typename T, typename Func>
bool dispatch_by_name(T& obj, std::string_view key, Func&& func) {
  using U = ylt::reflection::remove_cvref_t<T>;
  static constexpr auto members = data_members_array<U>();
  if constexpr (members.size() == 0) {
    return false;
  }
  else {
    static constexpr auto names = ylt::reflection::get_member_names<U>();
    bool found = false;
    [[maybe_unused]] std::size_t index = 0;
    template for (constexpr auto member : members) {
      if (!found && key == normalized_member_name(names[index])) {
        invoke_dispatch(func, obj.[:member:], names[index], index);
        found = true;
      }
      ++index;
    }
    return found;
  }
}

template <std::size_t Index, typename T, typename Func>
void dispatch_by_index(T& obj, Func&& func) {
  static constexpr auto members = data_members_array<T>();
  static_assert(Index < members.size(), "index out of range");
  func(obj.[:members[Index]:]);
}

template <typename T, typename Func>
bool dispatch_by_index(T& obj, std::size_t target, Func&& func) {
  static constexpr auto members = data_members_array<T>();
  if constexpr (members.size() == 0) {
    return false;
  }
  else {
    bool found = false;
    [[maybe_unused]] std::size_t index = 0;
    template for (constexpr auto member : members) {
      if (!found && index == target) {
        invoke_dispatch(func, obj.[:member:], member_name<member>(), index);
        found = true;
      }
      ++index;
    }
    return found;
  }
}

}  // namespace ylt::reflection::reflect26
#endif  // YLT_USE_CXX26_REFLECTION
