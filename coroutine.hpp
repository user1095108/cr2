#ifndef COROUTINE_HPP
# define COROUTINE_HPP
# pragma once

#include <cassert>
#include <cstddef>
#include <functional>
#include <memory>
#include <type_traits>

#include "generic/forwarder.hpp"
#include "generic/savestate.hpp"
#include "generic/scopeexit.hpp"

#include <event2/event.h>
#include <event2/event_struct.h>

namespace cr2
{

enum : std::size_t { default_stack_size = 1024 * 1024 };

enum state {DEAD, NEW, RUNNING, SUSPENDED, PAUSED};

static inline struct event_base* base;

namespace detail
{

struct empty_t{};

}

template <typename F>
class coroutine
{
private:
  gnr::statebuf in_, out_;

  enum state state_{};

  std::function<void(coroutine&)> f_;
  void* r_;

  std::unique_ptr<void*[]> stack_;
  std::size_t const N_;

public:
  explicit coroutine(F&& f, std::size_t const s = default_stack_size):
    state_{NEW},
    N_(s / sizeof(void*))
  {
    cr2::base = cr2::base ? cr2::base : event_base_new();

    using R = decltype(f(*this));

    if constexpr(std::is_void_v<R>)
    {
      f_ = std::forward<decltype(f)>(f);
    }
    else if constexpr(std::is_pointer_v<R>)
    {
      f_ = [this, f(std::forward<decltype(f)>(f))](coroutine& c)
        {
          r_ = const_cast<void*>(static_cast<void const*>(f(*this)));
        };
    }
    else if constexpr(std::is_reference_v<R>)
    {
      f_ = [this, f(std::forward<decltype(f)>(f))](coroutine& c)
        {
          r_ = const_cast<void*>(static_cast<void const*>(&f(*this)));
        };
    }
    else
    {
      f_ = [this, f(std::forward<decltype(f)>(f)), r(R())](
        coroutine& c) mutable
        {
          r_ = const_cast<void*>(static_cast<void const*>(&r));
          r = f(c);
        };
    }
  }

  coroutine(coroutine const&) = delete;
  coroutine(coroutine&&) = default;

  //
  explicit operator bool() const noexcept { return bool(state_); }

  void __attribute__ ((noinline)) operator()() noexcept
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
      stack_.reset(::new void*[N_]);
      state_ = RUNNING;

#if defined(__GNUC__)
# if defined(i386) || defined(__i386) || defined(__i386__)
      asm volatile(
        "movl %0, %%esp"
        :
        : "r" (&stack_[N_])
      );
# elif defined(__amd64__) || defined(__amd64) || defined(__x86_64__) ||\
    defined(__x86_64)
      asm volatile(
        "movq %0, %%rsp"
        :
        : "r" (&stack_[N_])
      );
# elif defined(__aarch64__) || defined(__arm__)
      asm volatile(
        "mov sp, %0"
        :
        : "r" (&stack_[N_])
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

  //
  template <typename R, bool Tuple = false>
  decltype(auto) retval() const
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
  void reset() noexcept { stack_.reset(); state_ = NEW; }

  void suspend(enum state const state = SUSPENDED) noexcept
  {
    if (savestate(in_))
    {
      clobber_all();
    }
    else
    {
      state_ = state;
      restorestate(out_);
    }
  }

  bool suspend_on(short, auto const...) noexcept;

  template <typename U>
  void suspend_to(coroutine<U>& c) noexcept
  {
    if (savestate(in_))
    {
      clobber_all();
    }
    else
    {
      state_ = SUSPENDED;
      c();
    }
  }
};

namespace detail
{

extern "C"
inline void do_cb(evutil_socket_t, short, void* const arg) noexcept
{
  (*static_cast<gnr::forwarder<void()>*>(arg))();
}

template <bool Tuple = false, typename F>
decltype(auto) retval(coroutine<F>& c) noexcept
{
  return c.template retval<decltype(std::declval<F>()(c)), Tuple>();
}

}

template <typename F>
inline bool coroutine<F>::suspend_on(short const flags,
  auto const ...fd) noexcept
{
  gnr::forwarder<void() noexcept> f(
    [&]() noexcept
    {
      if (PAUSED == state())
      {
        state_ = SUSPENDED;
        (*this)();
      }
    }
  );

  if (struct event ev[sizeof...(fd)];
    [&]<auto ...I>(std::index_sequence<I...>) noexcept
    {
      return (
        (
          event_assign(&ev[I], base, fd, flags, detail::do_cb, &f),
          (-1 == event_add(&ev[I], {}))
        ) ||
        ...
      );
    }(std::make_index_sequence<sizeof...(fd)>())
  )
  {
    return true;
  }
  else
  {
    return suspend(PAUSED), false;
  }
}

decltype(auto) await(auto&& ...c)
  requires(sizeof...(c) >= 1)
{
  while ((c || ...)) // while any c are still alive
  {
    (((NEW == c.state()) || (SUSPENDED == c.state()) ? c() : void(0)), ...);

    event_base_loop(base, EVLOOP_NONBLOCK); // process events
  }

  auto const reset_all(
    [&]() noexcept
    {
      (c.reset(), ...);
    }
  );

  SCOPE_EXIT(&, reset_all());

  if constexpr(sizeof...(c) > 1)
  {
    return std::tuple<decltype(detail::retval<true>(c))...>{
      detail::retval<true>(c)...
    };
  }
  else
  {
    return detail::retval((c, ...));
  }
}

}

#endif // COROUTINE_HPP
