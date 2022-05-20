#ifndef CR2_BASIC_COROUTINE_HPP
# define CR2_BASIC_COROUTINE_HPP
# pragma once

#include <cstddef> // std::size_t
#include <memory> // std::unique_ptr, std::shared_ptr
#include <tuple>

#include "generic/savestate.hpp"
#include "generic/scopeexit.hpp"

#include "common.hpp"

namespace cr2
{

template <typename F, typename R, std::size_t S>
class basic_coroutine
{
private:
  enum : std::size_t { N = S / sizeof(void*) };

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
        std::aligned_storage_t<sizeof(R), alignof(R)>
      >
    >
  > r_;

  alignas(std::max_align_t) void* stack_[N];

  //
  void destroy()
    noexcept(std::is_nothrow_destructible_v<R>)
  {
    if (NEW != state_)
    {
      std::destroy_at(std::launder(reinterpret_cast<R*>(&r_)));
    }
  }

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
  explicit basic_coroutine(F&& f)
    noexcept(noexcept(std::is_nothrow_move_constructible_v<F>)):
    state_{NEW},
    f_(std::move(f))
  {
  }

  ~basic_coroutine()
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

  basic_coroutine(basic_coroutine const&) = delete;
  basic_coroutine(basic_coroutine&&) = default;

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
  void reset() noexcept(noexcept(destroy()))
  {
    if constexpr(
      !std::is_pointer_v<R> &&
      !std::is_reference_v<R> &&
      !std::is_same_v<detail::empty_t, R>
    )
    {
      destroy();
    }

    state_ = NEW;
  }

  void suspend() noexcept { suspend<SUSPENDED>(); }

  template <typename A, typename B, std::size_t C>
  void suspend_to(basic_coroutine<A, B, C>& c) noexcept
  { // suspend means "out"
    c(); suspend();
  }
};

namespace basic
{

template <std::size_t S = default_stack_size>
auto make_plain(auto&& f)
  noexcept(noexcept(
      basic_coroutine<
        std::remove_cvref_t<decltype(f)>,
        detail::transform_void_t<
          decltype(
            std::declval<std::remove_cvref_t<decltype(f)>>()(
              std::declval<
                basic_coroutine<std::remove_cvref_t<decltype(f)>, detail::empty_t, S>&
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
        std::declval<basic_coroutine<F, detail::empty_t, S>&>()
      )
    )
  >;
  using C = basic_coroutine<F, R, S>;

  return C(std::forward<decltype(f)>(f));
}

template <std::size_t S = default_stack_size>
auto make_shared(auto&& f)
{
  using F = std::remove_cvref_t<decltype(f)>;
  using R = detail::transform_void_t<
    decltype(std::declval<F>()(
        std::declval<basic_coroutine<F, detail::empty_t, S>&>()
      )
    )
  >;
  using C = basic_coroutine<F, R, S>;

  return std::make_shared<C>(std::forward<decltype(f)>(f));
}

template <std::size_t S = default_stack_size>
auto make_unique(auto&& f)
{
  using F = std::remove_cvref_t<decltype(f)>;
  using R = detail::transform_void_t<
    decltype(std::declval<F>()(
        std::declval<basic_coroutine<F, detail::empty_t, S>&>()
      )
    )
  >;
  using C = basic_coroutine<F, R, S>;

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

#endif // CR2_BASIC_COROUTINE_HPP
