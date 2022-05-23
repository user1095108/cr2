#ifndef CR2_LIBNONE_SUPPORT_HPP
# define CR2_LIBNONE_SUPPORT_HPP
# pragma once

#include "common2.hpp"

namespace cr2
{

auto run(auto&& ...c)
  noexcept(noexcept((c.template retval<>(), ...)))
  requires(sizeof...(c) >= 1)
{
  {
    bool s;

    do
    {
      s = {};

      (
        (
          (c.state() >= NEW ? c() : void()),
          (s = s || (SUSPENDED == c.state()))
        ),
        ...
      );
    } while (s);
  }

  if constexpr(sizeof...(c) > 1)
  {
    return std::tuple<decltype(c.template retval<true>())...>{
      c.template retval<true>()...
    };
  }
  else
  {
    return (c, ...).template retval<>();
  }
}

auto make_and_run(auto&& ...c)
  noexcept(noexcept(run(make_plain(std::forward<decltype(c)>(c))...)))
{
  return run(make_plain(std::forward<decltype(c)>(c))...);
}

template <std::size_t ...S>
auto make_and_run(auto&& ...c)
  noexcept(noexcept(run(make_plain(std::forward<decltype(c)>(c))...)))
  requires(sizeof...(S) == sizeof...(c))
{
  return run(make_plain<S>(std::forward<decltype(c)>(c))...);
}

}

#endif // CR2_LIBNONE_SUPPORT_HPP
