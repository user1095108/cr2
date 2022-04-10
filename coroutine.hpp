#ifndef CR2_COROUTINE_HPP
# define CR2_COROUTINE_HPP
# pragma once

#include <cstddef> // std::size_t
#include <algorithm>
#include <chrono>
#include <memory> // std::unique_ptr, std::shared_ptr

#include "generic/forwarder.hpp"
#include "generic/invoke.hpp"
#include "generic/savestate.hpp"

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
inline void socket_cb(evutil_socket_t const s, short const f,
  void* const arg) noexcept
{
  (*static_cast<gnr::forwarder<void(evutil_socket_t, short)>*>(arg))(s, f);
}

}

template <typename F, typename R, std::size_t S>
class coroutine
{
private:
  enum : std::size_t { N = 1024 * S / sizeof(void*) };

  struct empty_t{};

  gnr::statebuf in_, out_;

  enum state state_;

  F f_;

  [[no_unique_address]]	std::conditional_t<
    std::is_void_v<R>,
    empty_t,
    std::conditional_t<
      std::is_reference_v<R>,
      R*,
      R
    >
  > r_;

  alignas(std::max_align_t) void* stack_[N];

  void __attribute__((noinline)) execute() noexcept
  {
    if constexpr(std::is_void_v<R>)
    {
      f_(*this);
    }
    else if constexpr(std::is_reference_v<R>)
    {
      r_ = &f_(*this);
    }
    else
    {
      r_ = f_(*this);
    }

    state_ = DEAD;
  }

  template <enum state State>
  void suspend() noexcept
#ifdef __clang__
    __attribute__((noinline))
#endif
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
  explicit coroutine(F&& f)
    noexcept(noexcept(std::is_nothrow_move_constructible_v<F>)):
    state_{NEW},
    f_(std::move(f))
  {
  }

  coroutine(coroutine const&) = delete;
  coroutine(coroutine&&) = default;

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
      return empty_t{};
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
      return R(std::move(r_));
    }
  }

  auto state() const noexcept { return state_; }

  //
  void reset() noexcept { state_ = NEW; }

  void pause() noexcept { suspend<PAUSED>(); }
  void suspend() noexcept { suspend<SUSPENDED>(); }

  template <class Rep, class Period>
  auto sleep(std::chrono::duration<Rep, Period> const d) noexcept
  {
    gnr::forwarder<void(evutil_socket_t, short) noexcept> f(
      [&](evutil_socket_t, short) noexcept
      {
        state_ = SUSPENDED;
      }
    );

    struct event ev;
    evtimer_assign(&ev, base, detail::socket_cb, &f);

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

  auto suspend_on(auto&& ...a) noexcept
    requires(!(sizeof...(a) % 2))
  {
    auto t([&]<auto ...I>(std::index_sequence<I...>) noexcept
      {
        return std::tuple{(I % 2 ? a : short{})...};
      }(std::make_index_sequence<sizeof...(a)>())
    );

    gnr::forwarder<void(evutil_socket_t, short) noexcept> f(
      [&](evutil_socket_t const s, short const f) noexcept
      {
        state_ = SUSPENDED;

        [&]<auto ...I>(std::index_sequence<I...>) noexcept
        {
          (
            (
              std::get<2 * I + 1>(t) == s ?
                void(std::get<2 * I>(t) = f) :
                void(0)
            ),
            ...
          );
        }(std::make_index_sequence<sizeof...(a) / 2>());
      }
    );

    struct event ev[sizeof...(a) / 2];

    if (gnr::invoke_split_cond<2>(
        [this, evp(&*ev), &f](auto&& flags, auto&& fd) mutable noexcept
        {
          event_assign(evp, base, fd, flags, detail::socket_cb, &f);

          return -1 == event_add(evp++, {});
        },
        std::forward<decltype(a)>(a)...
      )
    )
    {
      return [&]<auto ...I>(std::index_sequence<I...>) noexcept
        {
          return std::tuple{(I % 2 ? evutil_socket_t(-1) : short{})...};
        }(std::make_index_sequence<sizeof...(a)>());
    }
    else
    {
      pause();

      std::ranges::for_each(ev, [](auto& e) noexcept { event_del(&e); });

      return t;
    }
  }

  template <class Rep, class Period>
  auto suspend_on(std::chrono::duration<Rep, Period> const d,
    auto&& ...a) noexcept
    requires(!(sizeof...(a) % 2))
  {
    auto t([&]<auto ...I>(std::index_sequence<I...>) noexcept
      {
        return std::tuple{(I % 2 ? a : short{})...};
      }(std::make_index_sequence<sizeof...(a)>())
    );

    gnr::forwarder<void(evutil_socket_t, short) noexcept> f(
      [&](evutil_socket_t const s, short const f) noexcept
      {
        state_ = SUSPENDED;

        [&]<auto ...I>(std::index_sequence<I...>) noexcept
        {
          (
            (
              std::get<2 * I + 1>(t) == s ?
                void(std::get<2 * I>(t) = f) :
                void(0)
            ),
            ...
          );
        }(std::make_index_sequence<sizeof...(a) / 2>());
      }
    );

    struct timeval tv;
    tv.tv_sec = std::chrono::floor<std::chrono::seconds>(d).count();
    tv.tv_usec = std::chrono::duration_cast<std::chrono::microseconds>(
      d - std::chrono::floor<std::chrono::seconds>(d)).count();

    struct event ev[sizeof...(a) / 2];

    if (gnr::invoke_split_cond<2>(
        [this, evp(&*ev), &f, &tv](auto&& flags, auto&& fd) mutable noexcept
        {
          event_assign(evp, base, fd, flags, detail::socket_cb, &f);

          return -1 == event_add(evp++, &tv);
        },
        std::forward<decltype(a)>(a)...
      )
    )
    {
      return [&]<auto ...I>(std::index_sequence<I...>) noexcept
        {
          return std::tuple{(I % 2 ? evutil_socket_t(-1) : short{})...};
        }(std::make_index_sequence<sizeof...(a)>());
    }
    else
    {
      pause();

      std::ranges::for_each(ev, [](auto& e) noexcept { event_del(&e); });

      return t;
    }
  }

  template <typename A, typename B, std::size_t C>
  void suspend_to(coroutine<A, B, C>& c) noexcept
  { // suspend means "out"
    if (state_ = SUSPENDED; savestate(in_))
    {
      clobber_all()
    }
    else
    {
      c(); restorestate(out_);
    }
  }
};

template <std::size_t S = default_stack_size>
auto make_plain(auto&& f)
  noexcept(noexcept(
      coroutine<
        std::remove_cvref_t<decltype(f)>,
        decltype(
          std::declval<std::remove_cvref_t<decltype(f)>>()(
            std::declval<
              coroutine<std::remove_cvref_t<decltype(f)>, void, S>&
            >()
          )
        ),
        S
      >(std::forward<decltype(f)>(f))
    )
  )
{
  using F = std::remove_cvref_t<decltype(f)>;
  using R = decltype(
    std::declval<F>()(std::declval<coroutine<F, void, S>&>())
  );
  using C = coroutine<F, R, S>;

  return C(std::forward<decltype(f)>(f));
}

template <std::size_t S = default_stack_size>
auto make_shared(auto&& f)
{
  using F = std::remove_cvref_t<decltype(f)>;
  using R = decltype(
    std::declval<F>()(std::declval<coroutine<F, void, S>&>())
  );
  using C = coroutine<F, R, S>;

  return std::make_shared<C>(std::forward<decltype(f)>(f));
}

template <std::size_t S = default_stack_size>
auto make_unique(auto&& f)
{
  using F = std::remove_cvref_t<decltype(f)>;
  using R = decltype(
    std::declval<F>()(std::declval<coroutine<F, void, S>&>())
  );
  using C = coroutine<F, R, S>;

  return std::make_unique<C>(std::forward<decltype(f)>(f));
}

auto run(auto&& ...c)
  noexcept(noexcept((c.template retval<>(), ...)))
  requires(sizeof...(c) >= 1)
{
  for (base ? void(0) : void(base = event_base_new());;)
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

    if (r || p)
    {
      event_base_loop(base, r ? EVLOOP_NONBLOCK : EVLOOP_ONCE);
    }
    else
    {
      break;
    }
  }

  (c.reset(), ...);

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

#endif // CR2_COROUTINE_HPP
