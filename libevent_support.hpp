#ifndef CR2_LIBEVENT_SUPPORT_HPP
# define CR2_LIBEVENT_SUPPORT_HPP
# pragma once

#include <event2/event.h>
#include <event2/event_struct.h>
#include <event2/thread.h>

#include <chrono>
#include <concepts>

#include "generic/invoke.hpp"

#include "common2.hpp"

namespace cr2
{

static inline struct event_base* base;

extern "C"
{

inline void socket_cb(evutil_socket_t const s, short const f,
  void* const arg) noexcept
{
  (*static_cast<gnr::forwarder<void(evutil_socket_t, short)>*>(arg))(s, f);
}

inline void timer_cb(evutil_socket_t, short, void* const arg) noexcept
{
  (*static_cast<gnr::forwarder<void()>*>(arg))();
}

}

template <class D>
concept duration_c = requires(D d)
  {
    []<class A, class B>(std::chrono::duration<A, B>){}(d);
  };

template <typename T>
concept event_c = std::is_base_of_v<struct event, std::remove_pointer_t<T>>;

template <typename T>
concept integral_c = std::integral<std::remove_cvref_t<T>>;

//
auto await(auto& c, duration_c auto const d)
  noexcept(noexcept(c.pause()))
{
  gnr::forwarder<void() noexcept> f(
    [&]() noexcept
    {
      c.unpause();
    }
  );

  struct event ev;
  evtimer_assign(&ev, base, timer_cb, &f);

  struct timeval tv{
    .tv_sec = std::chrono::floor<std::chrono::seconds>(d).count(),
    .tv_usec = std::chrono::duration_cast<std::chrono::microseconds>(
      d - std::chrono::floor<std::chrono::seconds>(d)).count()
  };

  return -1 == event_add(&ev, &tv) ? true : (c.pause(), false);
}

auto await(auto& c, integral_c auto&& ...a)
  noexcept(noexcept(c.pause()))
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

      c.unpause();
    }
  );

  struct event ev[sizeof...(a) / 2];

  if (gnr::invoke_split_cond<2>(
      [ep(&*ev), &f](auto&& flags, auto&& fd) mutable noexcept
      {
        event_assign(ep, base, fd, EV_PERSIST|flags, socket_cb, &f);

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
    c.pause();

    std::ranges::for_each(ev, [](auto& e) noexcept { event_del(&e); });
  }

  return t;
}

auto await(auto& c, duration_c auto const d,
  integral_c auto&& ...a)
  noexcept(noexcept(c.pause()))
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

      c.unpause();
    }
  );

  struct timeval tv{
    .tv_sec = std::chrono::floor<std::chrono::seconds>(d).count(),
    .tv_usec = std::chrono::duration_cast<std::chrono::microseconds>(
      d - std::chrono::floor<std::chrono::seconds>(d)).count()
  };

  struct event ev[sizeof...(a) / 2];

  if (gnr::invoke_split_cond<2>(
      [ep(&*ev), &f, &tv](auto&& flags, auto&& fd) mutable noexcept
      {
        event_assign(ep, base, fd, EV_PERSIST|flags, socket_cb, &f);

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
    c.pause();

    std::ranges::for_each(ev, [](auto& e) noexcept { event_del(&e); });
  }

  return t;
}

bool await(auto& c, event_c auto* ...ev)
  noexcept(noexcept(c.pause()))
  requires(bool(sizeof...(ev)))
{
  gnr::forwarder<void() noexcept> g(
    [&]() noexcept
    {
      c.unpause();
    }
  );

  (event_assign(ev, base, -1, EV_PERSIST, timer_cb, &g), ...);

  return (((-1 == event_add(ev, {})) || ...)) ?
    true :
    (c.pause(), (event_del(ev), ...), false);
}

bool await_all(auto& c, event_c auto* ...ev)
  noexcept(noexcept(c.pause()))
  requires(bool(sizeof...(ev)))
{
  std::size_t a{};

  gnr::forwarder<void() noexcept> g(
    [&]() noexcept
    {
      ++a;
      c.unpause();
    }
  );

  (event_assign(ev, base, -1, EV_PERSIST, timer_cb, &g), ...);

  if (((-1 == event_add(ev, {})) || ...))
  {
    return true;
  }
  else
  {
    do
    {
      c.pause();
    } while (a != sizeof...(ev));

    return (event_del(ev), ...), false;
  }
}

auto run(auto&& ...c)
  noexcept(noexcept((c.template retval<>(), ...)))
  requires(sizeof...(c) >= 1)
{
  {
    auto const b(base ? base : base = event_base_new());

    for (bool p, s;;)
    {
      p = s = {};

      (
        (
          (c.state() >= NEW ? c() : void()),
          (p = p || (PAUSED == c.state())),
          (s = s || (SUSPENDED == c.state()))
        ),
        ...
      );

      if (p || s)
      {
        event_base_loop(b, s ? EVLOOP_NONBLOCK : EVLOOP_ONCE);
      }
      else
      {
        break;
      }
    }
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

#endif // CR2_LIBEVENT_SUPPORT_HPP
