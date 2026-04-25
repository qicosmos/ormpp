#pragma once

#include <type_traits>

namespace ormpp {

template <typename DB>
struct db_execution_traits {
  static constexpr bool is_async = false;

  template <typename T>
  using awaitable_type = T;
};

template <typename DB>
inline constexpr bool is_async_db_v =
    db_execution_traits<std::remove_pointer_t<DB>>::is_async;

template <typename DB, typename T>
using db_awaitable_t =
    typename db_execution_traits<std::remove_pointer_t<DB>>::template
        awaitable_type<T>;

}  // namespace ormpp
