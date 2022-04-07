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

#include <netinet/in.h>
#include <sys/socket.h>
#include <fcntl.h>

#include <event2/event.h>

namespace cr2
{

enum : std::size_t { default_stack_size = 1024 * 1024 };

namespace detail
{

struct empty_t{};

}

template <typename F>
class coroutine
{
public:
  enum state {DEAD, NEW, RUNNING, SUSPENDED};

private:
  gnr::statebuf in_, out_;

  enum state state_{};

  std::function<void(coroutine&)> f_;
  void* r_;

  std::unique_ptr<void*[]> stack_;
  void* sp_;

public:
  coroutine(std::size_t const s):
    stack_{::new void*[s / sizeof(void*)]},
    sp_{&stack_[s / sizeof(void*)]}
  {
  }

  explicit coroutine(auto&& f, std::size_t const s = default_stack_size):
    stack_{::new void*[s / sizeof(void*)]},
    sp_{&stack_[s / sizeof(void*)]}
  {
    assign(std::forward<decltype(f)>(f));
  }

  coroutine(coroutine const&) = delete;
  coroutine(coroutine&&) = default;

  //
  explicit operator bool() const noexcept { return bool(state_); }

  //
  template <typename R>
  decltype(auto) retval() const
    noexcept(
      std::is_void_v<R> ||
      std::is_pointer_v<R> ||
      std::is_reference_v<R> ||
      std::is_nothrow_move_constructible_v<R>
    )
  {
    if constexpr(std::is_void_v<R>)
    {
      return;
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
      return *static_cast<R*>(r_);
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

    state_ = NEW;
  }

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
      state_ = RUNNING;

#if defined(__GNUC__)
# if defined(i386) || defined(__i386) || defined(__i386__)
      asm volatile(
        "movl %0, %%esp"
        :
        : "r" (sp_)
      );
# elif defined(__amd64__) || defined(__amd64) || defined(__x86_64__) ||\
    defined(__x86_64)
      asm volatile(
        "movq %0, %%rsp"
        :
        : "r" (sp_)
      );
# elif defined(__aarch64__) || defined(__arm__)
      asm volatile(
        "mov sp, %0"
        :
        : "r" (sp_)
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

  //
  bool suspend_on(struct event_base*, evutil_socket_t, short) noexcept;
};

auto make(auto&& f, std::size_t const sz = cr2::default_stack_size)
{
  return coroutine<decltype(f)>(std::forward<decltype(f)>(f), sz);
}

auto make_and_run(auto&& f, std::size_t const sz = cr2::default_stack_size)
{
  coroutine<decltype(f)> c(std::forward<decltype(f)>(f), sz);

  c();

  return c;
}

template <typename F>
decltype(auto) retval(coroutine<F>& c) noexcept
{
  using R = decltype(std::declval<F>()(c));

  return c.template retval<R>();
}

namespace detail
{

extern "C" void do_resume(evutil_socket_t const fd, short const event,
  void* const arg)
{
  (*static_cast<gnr::forwarder<void()>*>(arg))();
}

}

template <typename F>
inline bool coroutine<F>::suspend_on(
  struct event_base* const base,
  evutil_socket_t const fd,
  short const flags) noexcept
{
  char tmp[128];
  auto const ev(reinterpret_cast<struct event*>(tmp));

  gnr::forwarder<void()> f([this]() noexcept
    {
      if (coroutine::SUSPENDED == state())
      {
        (*this)();
      }
    }
  );

  event_assign(ev, base, fd, flags, detail::do_resume, &f);
  event_add(ev, {});

  suspend();

  return false;
}

}

#endif // COROUTINE_HPP
