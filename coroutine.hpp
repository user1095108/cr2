#ifndef COROUTINE_HPP
# define COROUTINE_HPP
# pragma once

#include <cassert>
#include <cstddef>
#include <functional>
#include <memory>
#include <type_traits>

#include "generic/savestate.hpp"
#include "generic/scopeexit.hpp"

namespace cr2
{

enum : std::size_t { default_stack_size = 1024 * 1024 };

namespace detail
{

using empty_t = struct{};

}

template <std::size_t S = default_stack_size>
class coroutine
{
public:
  enum : std::size_t { stack_size = S, N = S / sizeof(void*) };

  enum state {DEAD, NEW, RUNNING, SUSPENDED};

private:
  gnr::statebuf in_, out_;

  enum state state_{};

  std::function<void(coroutine&)> f_;
  void* r_;

  std::unique_ptr<void*[]> stack_{::new void*[N]};

public:
  coroutine() = default;

  explicit coroutine(auto&& f) { assign(std::forward<decltype(f)>(f)); }

  coroutine(coroutine const&) = delete;
  coroutine(coroutine&&) = default;

  //
  explicit operator bool() const noexcept { return bool(state_); }

  //
  template <typename R, bool Tuple>
  decltype(auto) return_val() const
    noexcept(
      std::is_void_v<R> ||
      std::is_pointer_v<R> ||
      std::is_reference_v<R> ||
      std::is_nothrow_move_constructible_v<R>
    )
  {
    if constexpr(std::is_void_v<R> && !Tuple)
    {
      return;
    }
    else if constexpr(std::is_void_v<R> && Tuple)
    {
      return detail::empty_t{};
    }
    else if constexpr(std::is_pointer_v<R>)
    {
      return static_cast<R>(r_);
    }
    else if constexpr(std::is_reference_v<R>)
    {
      return R(*static_cast<std::remove_reference_t<R>*>(r_));
    }
    else
    {
      return R(std::move(*static_cast<R*>(r_)));
    }
  }

  auto state() const noexcept { return state_; }

  //
  void assign(auto&& f)
  {
    using R = decltype(f(*this));

    if constexpr(std::is_void_v<R>)
    {
      f_ = std::forward<decltype(f)>(f);
    }
    else if constexpr(std::is_pointer_v<R>)
    {
      f_ = [this, f(std::forward<decltype(f)>(f))]()
        {
          r_ = const_cast<void*>(static_cast<void const*>(f(*this)));
        };
    }
    else if constexpr(std::is_reference_v<R>)
    {
      f_ = [this, f(std::forward<decltype(f)>(f))]()
        {
          r_ = const_cast<void*>(static_cast<void const*>(&f(*this)));
        };
    }
    else
    {
      f_ = [this, f(std::forward<decltype(f)>(f)), r(R())](
        coroutine& l) mutable
        {
          r_ = const_cast<void*>(static_cast<void const*>(&r));
          r = f(l);
        };
    }

    state_ = NEW;
  }

  void __attribute__ ((noinline)) resume() noexcept
  {
    if (savestate(out_))
    {
      clobber_all();
    }
    else if (SUSPENDED == state_)
    {
      state_ = RUNNING;
      restorestate(in_); // return inside
    }
    else
    {
      state_ = RUNNING;

#if defined(__GNUC__)
# if defined(i386) || defined(__i386) || defined(__i386__)
      asm volatile(
        "movl %0, %%esp"
        :
        : "r" (&stack_[N])
      );
# elif defined(__amd64__) || defined(__amd64) || defined(__x86_64__) ||\
    defined(__x86_64)
      asm volatile(
        "movq %0, %%rsp"
        :
        : "r" (&stack_[N])
      );
# elif defined(__aarch64__) || defined(__arm__)
      asm volatile(
        "mov sp, %0"
        :
        : "r" (&stack_[N])
      );
# else
#   error "can't switch stack frame"
# endif
#else
# error "can't switch stack frame"
#endif

      f_(*this);

      state_ = DEAD;
      restorestate(out_); // return outside
    }
  }

  void suspend() noexcept
  {
    if (savestate(in_))
    {
      clobber_all();
    }
    else
    {
      state_ = SUSPENDED;
      restorestate(out_);
    }
  }

  template <std::size_t U>
  void suspend_to(coroutine<U>& c) noexcept
  {
    if (savestate(in_))
    {
      clobber_all();
    }
    else
    {
      state_ = SUSPENDED;
      c.resume();
    }
  }
};

template <std::size_t S = default_stack_size>
auto make_coroutine(auto&& ...f)
  requires(bool(sizeof...(f)))
{
  if constexpr(sizeof...(f) > 1)
  {
    return std::tuple{coroutine<S>(std::forward<decltype(f)>(f))...};
  }
  else
  {
    return coroutine<S>(std::forward<decltype(f)>(f)...);
  }
}

}

#endif // COROUTINE_HPP
