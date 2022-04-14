#ifndef CR2_COROUTINE_HPP
# define CR2_COROUTINE_HPP
# pragma once

#include <cstddef> // std::size_t
#include <algorithm>
#include <chrono>
#include <concepts>
#include <memory> // std::unique_ptr, std::shared_ptr

#include "generic/forwarder.hpp"
#include "generic/invoke.hpp"
#include "generic/savestate.hpp"

#include <event2/event.h>
#include <event2/event_struct.h>
#include <event2/thread.h>

namespace cr2
{

enum : std::size_t { default_stack_size = 512 };

enum state {DEAD, RUNNING, PAUSED, NEW, SUSPENDED};

static inline struct event_base* base;

namespace detail
{

struct empty_t{};

template <typename T>
using transform_void_t = std::conditional_t<std::is_void_v<T>, empty_t, T>;

extern "C"
inline void socket_cb(evutil_socket_t const s, short const f,
  void* const arg) noexcept
{
  (*static_cast<gnr::forwarder<void(evutil_socket_t, short)>*>(arg))(s, f);
}

extern "C"
inline void timer_cb(evutil_socket_t, short, void* const arg) noexcept
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
        std::aligned_storage_t<sizeof(R)>
      >
    >
  > r_;

  alignas(std::max_align_t) void* stack_[N];

  __attribute__((noinline)) void execute() noexcept
  {
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
  }

  template <enum state State>
#ifdef __clang__
  __attribute__((noinline))
#endif
  void suspend() noexcept
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

  ~coroutine()
    noexcept(
      std::is_pointer_v<R> ||
      std::is_reference_v<R> ||
      std::is_same_v<detail::empty_t, R> ||
      noexcept(reinterpret_cast<R*>(&r_)->~R())
    )
  {
    if constexpr(
      !std::is_pointer_v<R> &&
      !std::is_reference_v<R> &&
      !std::is_same_v<detail::empty_t, R>
    )
    {
      reinterpret_cast<R*>(&r_)->~R();
    }
  }

  coroutine(coroutine const&) = delete;
  coroutine(coroutine&&) = default;

  explicit operator bool() const noexcept { return bool(state_); }

  __attribute__((noinline)) void operator()() noexcept
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
  void reset() noexcept { state_ = NEW; }

  void pause() noexcept { suspend<PAUSED>(); }
  void suspend() noexcept { suspend<SUSPENDED>(); }

  //
  template <class Rep, class Period>
  auto await(std::chrono::duration<Rep, Period> const d) noexcept
  {
    gnr::forwarder<void() noexcept> f(
      [&]() noexcept
      {
        state_ = SUSPENDED;
      }
    );

    struct event ev;
    evtimer_assign(&ev, base, detail::timer_cb, &f);

    struct timeval tv{
      .tv_sec = std::chrono::floor<std::chrono::seconds>(d).count(),
      .tv_usec = std::chrono::duration_cast<std::chrono::microseconds>(
        d - std::chrono::floor<std::chrono::seconds>(d)).count()
    };

    return -1 == event_add(&ev, &tv) ? true : (pause(), false);
  }

  auto await(auto&& ...a) noexcept
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
                void()
            ),
            ...
          );
        }(std::make_index_sequence<sizeof...(a) / 2>());
      }
    );

    struct event ev[sizeof...(a) / 2];

    if (gnr::invoke_split_cond<2>(
        [this, ep(&*ev), &f](auto&& flags, auto&& fd) mutable noexcept
        {
          event_assign(ep, base, fd, EV_PERSIST|flags, detail::socket_cb, &f);

          return -1 == event_add(ep++, {});
        },
        std::forward<decltype(a)>(a)...
      )
    )
    {
      [&]<auto ...I>(std::index_sequence<I...>) noexcept
      { // set sockets to -1
        (
          (
            std::get<2 * I + 1>(t) = -1
          ),
          ...
        );
      }(std::make_index_sequence<sizeof...(a) / 2>());
    }
    else
    {
      pause();

      std::ranges::for_each(ev, [](auto& e) noexcept { event_del(&e); });
    }

    return t;
  }

  template <class Rep, class Period>
  auto await(std::chrono::duration<Rep, Period> const d, auto&& ...a) noexcept
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
                void()
            ),
            ...
          );
        }(std::make_index_sequence<sizeof...(a) / 2>());
      }
    );

    struct timeval tv{
      .tv_sec = std::chrono::floor<std::chrono::seconds>(d).count(),
      .tv_usec = std::chrono::duration_cast<std::chrono::microseconds>(
        d - std::chrono::floor<std::chrono::seconds>(d)).count()
    };

    struct event ev[sizeof...(a) / 2];

    if (gnr::invoke_split_cond<2>(
        [this, ep(&*ev), &f, &tv](auto&& flags, auto&& fd) mutable noexcept
        {
          event_assign(ep, base, fd, EV_PERSIST|flags, detail::socket_cb, &f);

          return -1 == event_add(ep++, &tv);
        },
        std::forward<decltype(a)>(a)...
      )
    )
    {
      [&]<auto ...I>(std::index_sequence<I...>) noexcept
      { // set sockets to -1
        (
          (
            std::get<2 * I + 1>(t) = -1
          ),
          ...
        );
      }(std::make_index_sequence<sizeof...(a) / 2>());
    }
    else
    {
      pause();

      std::ranges::for_each(ev, [](auto& e) noexcept { event_del(&e); });
    }

    return t;
  }

  bool await(auto&& f, auto* ...ev)
    noexcept(noexcept(f()))
    requires(bool(sizeof...(ev)))
  {
    gnr::forwarder<void() noexcept> g(
      [&]() noexcept
      {
        state_ = SUSPENDED;
      }
    );

    (event_assign(ev, base, -1, EV_PERSIST, detail::timer_cb, &g), ...);

    return (((-1 == event_add(ev, {})) || ...)) ?
      true :
      (f(), pause(), (event_del(ev), ...), false);
  }

  bool await_all(auto&& f, auto* ...ev)
    noexcept(noexcept(f()))
    requires(bool(sizeof...(ev)))
  {
    std::size_t c{};

    gnr::forwarder<void() noexcept> g(
      [&]() noexcept
      {
        state_ = SUSPENDED;
        ++c;
      }
    );

    (event_assign(ev, base, -1, EV_PERSIST, detail::timer_cb, &g), ...);

    if (((-1 == event_add(ev, {})) || ...))
    {
      return true;
    }
    else
    {
      f();

      do
      {
        pause();
      } while (c != sizeof...(ev));

      return (event_del(ev), ...), false;
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

auto run(auto&& ...c)
  noexcept(noexcept((c.template retval<>(), ...)))
  requires(sizeof...(c) >= 1)
{
  for (auto const b(base ? base : base = event_base_new());;)
  {
    std::size_t p{}, s{};

    (
      (
        (c.state() >= NEW ? c() : void()),
        (SUSPENDED == c.state() ? ++s : p += PAUSED == c.state())
      ),
      ...
    );

    if (s || p)
    {
      event_base_loop(b, s ? EVLOOP_NONBLOCK : EVLOOP_ONCE);
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
