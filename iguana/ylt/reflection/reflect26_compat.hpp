#pragma once
// GCC 16 uses __cpp_impl_reflection; future compilers may use __cpp_reflection
#if (defined(__cpp_impl_reflection) && __cpp_impl_reflection >= 202406L) || \
    (defined(__cpp_reflection) && __cpp_reflection >= 202406L)
#define YLT_USE_CXX26_REFLECTION 1
#endif
