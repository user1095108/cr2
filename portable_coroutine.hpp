#ifndef CR2_PORTABLE_COROUTINE_HPP
# define CR2_PORTABLE_COROUTINE_HPP
# pragma once

#include <cstddef> // std::size_t

#include "boost/context/fiber.hpp"

#include "common.hpp"

namespace cr2
{

template <typename F, typename R, std::size_t S>
class coroutine
{
  template <typename, typename, std::size_t>
  friend class coroutine;

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
    static_assert(
      !std::is_pointer_v<R> &&
      !std::is_reference_v<R> &&
      !std::is_same_v<detail::empty_t, R>
    );

    if (DEAD == state_)
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
  explicit coroutine(F&& f):
    state_{NEW},
    f_(std::move(f))
  {
    reset();
  }

  ~coroutine()
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

  coroutine(coroutine const&) = delete;
  coroutine(coroutine&&) = default;

  explicit operator bool() const noexcept { return bool(state_); }

  void operator()() noexcept
  {
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
  void pause() { suspend<PAUSED>(); }
  void unpause() noexcept { state_ = SUSPENDED; }

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
  void suspend_to(coroutine<A, B, C>& c)
  {
    c(); suspend();
  }
};

}

#endif // CR2_PORTABLE_COROUTINE_HPP
