#ifndef CR2_COMMON2_HPP
# define CR2_COMMON2_HPP
# pragma once

#include <algorithm>

#include "generic/forwarder.hpp"
#include "generic/scopeexit.hpp"

namespace cr2
{

template <std::size_t S = default_stack_size>
auto make_plain(auto&& f)
  noexcept(noexcept(
      coroutine<
        std::remove_cvref_t<decltype(f)>,
        detail::transform_void_t<
          decltype(
            std::declval<std::remove_cvref_t<decltype(f)>>()(
              std::declval<
                coroutine<std::remove_cvref_t<decltype(f)>, detail::empty_t, S>&
              >()
            )
          )
        >,
        S
      >(std::forward<decltype(f)>(f))
    )
  )
{
  using F = std::remove_cvref_t<decltype(f)>;
  using R = detail::transform_void_t<
    decltype(std::declval<F>()(
        std::declval<coroutine<F, detail::empty_t, S>&>()
      )
    )
  >;
  using C = coroutine<F, R, S>;

  return C(std::forward<decltype(f)>(f));
}

template <std::size_t S = default_stack_size>
auto make_shared(auto&& f)
{
  using F = std::remove_cvref_t<decltype(f)>;
  using R = detail::transform_void_t<
    decltype(std::declval<F>()(
        std::declval<coroutine<F, detail::empty_t, S>&>()
      )
    )
  >;
  using C = coroutine<F, R, S>;

  return std::make_shared<C>(std::forward<decltype(f)>(f));
}

template <std::size_t S = default_stack_size>
auto make_unique(auto&& f)
{
  using F = std::remove_cvref_t<decltype(f)>;
  using R = detail::transform_void_t<
    decltype(std::declval<F>()(
        std::declval<coroutine<F, detail::empty_t, S>&>()
      )
    )
  >;
  using C = coroutine<F, R, S>;

  return std::make_unique<C>(std::forward<decltype(f)>(f));
}

}

#endif // CR2_COMMON2_HPP
