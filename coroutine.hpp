#ifndef COROUTINE_HPP
# define COROUTINE_HPP
# pragma once

#include <cstddef> // std::size_t
#include <algorithm>
#include <chrono>
#include <memory> // std::unique_ptr

#include "generic/forwarder.hpp"
#include "generic/invoke.hpp"
#include "generic/savestate.hpp"
#include "generic/scopeexit.hpp"

#include <event2/event.h>
#include <event2/event_struct.h>

namespace cr2
{

enum : std::size_t { default_stack_size = 512 };

enum state {DEAD, RUNNING, PAUSED, NEW, SUSPENDED};

static inline struct event_base* base;

namespace detail
{

extern "C"
inline void socket_cb(evutil_socket_t, short, void* const arg) noexcept
{
  (*static_cast<gnr::forwarder<void()>*>(arg))();
}

}

template <typename F, typename R, std::size_t S>
class coroutine
{
private:
  enum : std::size_t { N = 1024 * S / sizeof(void*) };

  gnr::statebuf in_, out_;

  enum state state_;

  std::conditional_t<std::is_void_v<R>, void*, R> r_;

  F f_;

  alignas(std::max_align_t) void* stack_[N];

  coroutine(coroutine const&) = delete;
  coroutine(coroutine&&) = default;

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

    state_ = DEAD;
  }

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
  explicit coroutine(F&& f):
    state_{NEW},
    f_(std::move(f))
  {
    if (!cr2::base)
    {
      cr2::base = event_base_new();
    }
  }

  explicit operator bool() const noexcept { return bool(state_); }

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

      restorestate(out_); // return outside
    }
  }

  //
  template <bool Tuple = false>
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
      struct empty_t{}; return empty_t{};
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

  bool suspend_on(auto&& ...a) noexcept
    requires(!(sizeof...(a) % 2))
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

    struct event ev[sizeof...(a) / 2];

    if (gnr::invoke_split_cond<2>(
        [evp(&*ev), &f, this](auto&& flags, auto&& fd) mutable noexcept
        {
          event_assign(evp, base, fd, flags, detail::socket_cb, &f);

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
      pause();

      std::ranges::for_each(ev, [](auto& e) noexcept { event_del(&e); });

      return false;
    }
  }

  template <typename A, typename B, std::size_t C>
  void suspend_to(coroutine<A, B, C>& c) noexcept
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

  template <class Rep, class Period>
  bool sleep(std::chrono::duration<Rep, Period> const d) noexcept
  {
    gnr::forwarder<void() noexcept> f([&]() noexcept { state_ = SUSPENDED; });

    struct event ev;
    event_assign(&ev, base, -1, 0, detail::socket_cb, &f);

    struct timeval tv;
    tv.tv_sec = std::chrono::floor<std::chrono::seconds>(d).count();
    tv.tv_usec = std::chrono::duration_cast<std::chrono::microseconds>(
      d - std::chrono::floor<std::chrono::seconds>(d)).count();

    if (-1 == event_add(&ev, &tv))
    {
      return true;
    }
    else
    {
      return pause(), false;
    }
  }
};

template <std::size_t S = default_stack_size>
auto make_coroutine(auto&& f)
{
  using F = std::remove_cvref_t<decltype(f)>;
  using R = decltype(
    std::declval<F>()(std::declval<coroutine<F, void, S>&>())
  );
  using C = coroutine<std::remove_cvref_t<decltype(f)>, R, S>;

  return C(std::forward<decltype(f)>(f));
}

template <std::size_t S = default_stack_size>
auto make_shared(auto&& f)
{
  using F = std::remove_cvref_t<decltype(f)>;
  using R = decltype(
    std::declval<F>()(std::declval<coroutine<F, void, S>&>())
  );
  using C = coroutine<std::remove_cvref_t<decltype(f)>, R, S>;

  return std::make_shared<C>(std::forward<decltype(f)>(f));
}

template <std::size_t S = default_stack_size>
auto make_unique(auto&& f)
{
  using F = std::remove_cvref_t<decltype(f)>;
  using R = decltype(
    std::declval<F>()(std::declval<coroutine<F, void, S>&>())
  );
  using C = coroutine<std::remove_cvref_t<decltype(f)>, R, S>;

  return std::make_unique<C>(std::forward<decltype(f)>(f));
}

decltype(auto) run(auto&& ...c)
  noexcept(noexcept((c.template retval<>(), ...)))
  requires(sizeof...(c) >= 1)
{
  for (;;)
  {
    std::size_t p{}, r{};

    (
      (
        c.state() >= NEW ?
          ++r, c() :
          void(p += PAUSED == c.state())
      ),
      ...
    );

    if (p || r)
    {
      event_base_loop(base, r ? EVLOOP_NONBLOCK : EVLOOP_ONCE);
    }
    else
    {
      break;
    }
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
    return std::tuple<decltype(c.template retval<true>())...>{
      c.template retval<true>()...
    };
  }
  else
  {
    return (c, ...).template retval<>();
  }
}

}

#endif // COROUTINE_HPP
