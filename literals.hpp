#ifndef CR2_LITERALS_HPP
# define CR2_LITERALS_HPP
# pragma once

namespace cr2::literals
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

#endif // CR2_LITERALS_HPP
