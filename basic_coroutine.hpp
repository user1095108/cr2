#ifndef CR2_BASIC_COROUTINE_HPP
# define CR2_BASIC_COROUTINE_HPP
# pragma once

#include "generic/savestate.hpp"

#include "common.hpp"

namespace cr2
{

template <typename F, typename R, std::size_t S>
class coroutine
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
    if (DEAD == state_)
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
  void const* id() const noexcept { return this; }

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
  void pause() noexcept { suspend<PAUSED>(); }
  void unpause() noexcept { state_ = SUSPENDED; }

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
  void suspend_to(coroutine<A, B, C>& c) noexcept
  { // suspend means "out"
    c(); suspend();
  }
};

}

#endif // CR2_BASIC_COROUTINE_HPP
