#ifndef CR2_COMMON_HPP
# define CR2_COMMON_HPP
# pragma once

#include <cstddef> // std::size_t

namespace cr2
{

enum : std::size_t { default_stack_size = 512 * 1024 };

enum state {DEAD, RUNNING, PAUSED, NEW, SUSPENDED};

namespace detail
{

struct empty_t{};

template <typename T>
using transform_void_t = std::conditional_t<std::is_void_v<T>, empty_t, T>;

}

namespace literals
{

constexpr std::size_t operator ""_k(unsigned long long const a) noexcept
{
  return 1024ULL * a;
}

constexpr std::size_t operator ""_M(unsigned long long const a) noexcept
{
  return 1024ULL * 1024ULL * a;
}

}

}

#endif // CR2_COMMON_HPP
