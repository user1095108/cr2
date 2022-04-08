#ifndef COROUTINE_HPP
# define COROUTINE_HPP
# pragma once

#include <cstddef> // std::size_t
#include <memory> // std::unique_ptr

#include "generic/forwarder.hpp"
#include "generic/invoke.hpp"
#include "generic/savestate.hpp"
#include "generic/scopeexit.hpp"

#include <event2/event.h>
#include <event2/event_struct.h>

namespace cr2
{

enum : std::size_t { default_stack_size = 1024 * 1024 };

enum state {DEAD, NEW, RUNNING, SUSPENDED, PAUSED};

static inline struct event_base* base;

template <std::size_t = default_stack_size> auto make_coroutine(auto&&);

template <typename F, std::size_t S>
class coroutine
{
private:
  template <std::size_t> friend  auto make_coroutine(auto&&);

  enum : std::size_t { N = S / sizeof(void*) };

  gnr::statebuf in_, out_;

  enum state state_;

  using R = decltype(std::declval<F>()(std::declval<coroutine&>()));
  std::conditional_t<std::is_void_v<R>, void*, R> r_;

  F f_;

  alignas(std::max_align_t) void* stack_[N];
  //std::unique_ptr<void*[]> stack_{::new void*[N]};

  explicit coroutine(F&& f):
    state_{NEW},
    f_(std::move(f))
  {
    if (!cr2::base)
    {
      cr2::base = event_base_new();
    }
  }

  coroutine(coroutine const&) = delete;
  coroutine(coroutine&&) = default;

  template <enum state State>
  void set_state() noexcept
  {
    if (state_ = State; savestate(in_))
    {
      clobber_all();
    }
    else
    {
      restorestate(out_);
    }
  }

public:
  explicit operator bool() const noexcept { return bool(state_); }

  void __attribute__((noinline)) execute() noexcept
  {
    if constexpr(std::is_void_v<R>)
    {
      f_(*this);
    }
    else
    {
      r_ = f_(*this);
    }
  }

  void __attribute__((noinline)) operator()() noexcept
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

      execute();

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
      struct empty_t{};
      return empty_t{};
    }
    else
    {
      return R(std::move(r_));
    }
  }

  auto state() const noexcept { return state_; }

  //
  void reset() noexcept { state_ = NEW; }

  void pause() noexcept { set_state<PAUSED>(); }
  void suspend() noexcept { set_state<SUSPENDED>(); }

  template <typename A, std::size_t B>
  void suspend_to(coroutine<A, B>& c) noexcept
  {
    if (state_ = SUSPENDED; savestate(in_))
    {
      clobber_all()
    }
    else
    {
      c();
      restorestate(out_);
    }
  }

  bool suspend_on(auto&& ...) noexcept;
};

template <std::size_t S>
auto make_coroutine(auto&& f)
{
  return coroutine<std::remove_cvref_t<decltype(f)>, S>(
    std::forward<decltype(f)>(f)
  );
}

namespace detail
{

extern "C"
inline void do_cb(evutil_socket_t, short, void* const arg) noexcept
{
  (*static_cast<gnr::forwarder<void()>*>(arg))();
}

template <bool Tuple = false, typename F, std::size_t S>
inline decltype(auto) retval(coroutine<F, S>& c)
  noexcept(
    noexcept(c.template retval<decltype(std::declval<F>()(c)), Tuple>())
  )
{
  return c.template retval<decltype(std::declval<F>()(c)), Tuple>();
}

}

template <typename F, std::size_t S>
inline bool coroutine<F, S>::suspend_on(auto&& ...a) noexcept
{
  gnr::forwarder<void() noexcept> f(
    [&]() noexcept
    {
      if (PAUSED == state())
      {
        state_ = SUSPENDED;
      }
    }
  );

  struct event ev[sizeof...(a)];

  if (auto evp(&*ev); gnr::invoke_split_cond<2>(
      [&](auto&& flags, auto&& fd) mutable noexcept
      {
        event_assign(evp, base, fd, flags, detail::do_cb, &f);
        return -1 == event_add(evp++, {});
      },
      std::forward<decltype(a)>(a)...
    )
  )
  {
    return true;
  }
  else
  {
    return pause(), false;
  }
}

decltype(auto) await(auto&& ...c)
  noexcept(noexcept((detail::retval(c), ...)))
  requires(sizeof...(c) >= 1)
{
  while ((
    (((NEW == c.state()) || (SUSPENDED == c.state()) ?  c() : void(0)), c) ||
    ...)
  )
  {
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
