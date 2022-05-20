#ifndef CR2_PORTABLE_COROUTINE_HPP
# define CR2_PORTABLE_COROUTINE_HPP
# pragma once

#include <cstddef> // std::size_t
#include <memory> // std::unique_ptr, std::shared_ptr
#include <tuple>

#include "boost/context/fiber.hpp"

#include "generic/scopeexit.hpp"

#include "common.hpp"

namespace cr2
{

template <typename F, typename R, std::size_t S>
class portable_coroutine
{
  template <typename, typename, std::size_t>
  friend class portable_coroutine;

private:
  boost::context::fiber fi_;

  enum state state_;

  F f_;

  [[no_unique_address]]	std::conditional_t<
    std::is_pointer_v<R>,
    R,
    std::conditional_t<
      std::is_reference_v<R>,
      R*,
      std::conditional_t<
        std::is_same_v<detail::empty_t, R>,
        detail::empty_t,
        std::aligned_storage_t<sizeof(R), alignof(R)>
      >
    >
  > r_;

  //
  void destroy()
    noexcept(std::is_nothrow_destructible_v<R>)
  {
    if (NEW != state_)
    {
      std::destroy_at(std::launder(reinterpret_cast<R*>(&r_)));
    }
  }

  template <enum state State>
  void suspend()
  {
    state_ = State;

    fi_ = std::move(fi_).resume();
  }

public:
  explicit portable_coroutine(F&& f)
    noexcept(noexcept(std::is_nothrow_move_constructible_v<F>)):
    state_{NEW},
    f_(std::move(f))
  {
    reset();
  }

  ~portable_coroutine()
    noexcept(
      std::is_pointer_v<R> ||
      std::is_reference_v<R> ||
      std::is_same_v<detail::empty_t, R> ||
      std::is_nothrow_destructible_v<R>
    )
  {
    if constexpr(
      !std::is_pointer_v<R> &&
      !std::is_reference_v<R> &&
      !std::is_same_v<detail::empty_t, R>
    )
    {
      destroy();
    }
  }

  portable_coroutine(portable_coroutine const&) = delete;
  portable_coroutine(portable_coroutine&&) = default;

  explicit operator bool() const noexcept { return bool(state_); }

  void operator()()
  {
    assert(state_ >= NEW);
    state_ = RUNNING;

    fi_ = std::move(fi_).resume();
  }

  //
  template <bool Tuple = false>
  decltype(auto) retval()
    noexcept(
      std::is_void_v<R> ||
      std::is_pointer_v<R> ||
      std::is_reference_v<R> ||
      std::is_nothrow_move_constructible_v<R>
    )
  {
    if constexpr(std::is_same_v<detail::empty_t, R> && !Tuple)
    {
      return;
    }
    else if constexpr(std::is_same_v<detail::empty_t, R>)
    {
      return detail::empty_t{};
    }
    else if constexpr(std::is_pointer_v<R>)
    {
      return r_;
    }
    else if constexpr(std::is_reference_v<R>)
    {
      return R(*r_);
    }
    else
    {
      return R(std::move(*reinterpret_cast<R*>(&r_)));
    }
  }

  auto state() const noexcept { return state_; }

  //
  void reset()
  {
    if constexpr(
      !std::is_pointer_v<R> &&
      !std::is_reference_v<R> &&
      !std::is_same_v<detail::empty_t, R>
    )
    {
      destroy();
    }

    fi_ = {
      std::allocator_arg_t{},
      boost::context::fixedsize_stack(S),
      [this](auto&& fi)
      {
        fi_ = std::move(fi);

        if constexpr(std::is_same_v<detail::empty_t, R>)
        {
          f_(*this);
        }
        else if constexpr(std::is_pointer_v<R>)
        {
          r_ = f_(*this);
        }
        else if constexpr(std::is_reference_v<R>)
        {
          r_ = &f_(*this);
        }
        else
        {
          ::new (std::addressof(r_)) R(f_(*this));
        }

        state_ = DEAD;

        return std::move(fi_);
      }
    };

    state_ = NEW;
  }

  void suspend() { return suspend<SUSPENDED>(); }

  template <typename A, typename B, std::size_t C>
  void suspend_to(portable_coroutine<A, B, C>& c)
  { // suspend means "out"
    c(); suspend();
  }
};

namespace portable
{

template <std::size_t S = default_stack_size>
auto make_plain(auto&& f)
  noexcept(noexcept(
      portable_coroutine<
        std::remove_cvref_t<decltype(f)>,
        detail::transform_void_t<
          decltype(
            std::declval<std::remove_cvref_t<decltype(f)>>()(
              std::declval<
                portable_coroutine<std::remove_cvref_t<decltype(f)>, detail::empty_t, S>&
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
        std::declval<portable_coroutine<F, detail::empty_t, S>&>()
      )
    )
  >;
  using C = portable_coroutine<F, R, S>;

  return C(std::forward<decltype(f)>(f));
}

template <std::size_t S = default_stack_size>
auto make_shared(auto&& f)
{
  using F = std::remove_cvref_t<decltype(f)>;
  using R = detail::transform_void_t<
    decltype(std::declval<F>()(
        std::declval<portable_coroutine<F, detail::empty_t, S>&>()
      )
    )
  >;
  using C = portable_coroutine<F, R, S>;

  return std::make_shared<C>(std::forward<decltype(f)>(f));
}

template <std::size_t S = default_stack_size>
auto make_unique(auto&& f)
{
  using F = std::remove_cvref_t<decltype(f)>;
  using R = detail::transform_void_t<
    decltype(std::declval<F>()(
        std::declval<portable_coroutine<F, detail::empty_t, S>&>()
      )
    )
  >;
  using C = portable_coroutine<F, R, S>;

  return std::make_unique<C>(std::forward<decltype(f)>(f));
}

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

  auto const l(
    [&]() noexcept(noexcept((c.reset(), ...)))
    {
      (c.reset(), ...);
    }
  );

  SCOPE_EXIT(&, l());

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

}

#endif // CR2_PORTABLE_COROUTINE_HPP
