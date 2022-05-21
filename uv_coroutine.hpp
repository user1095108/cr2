#ifndef CR2_UV_COROUTINE_HPP
# define CR2_UV_COROUTINE_HPP
# pragma once

#include <uv.h>

#include <cstddef> // std::size_t
#include <memory> // std::unique_ptr, std::shared_ptr
#include <tuple>

#include "boost/context/fiber.hpp"

#include "generic/forwarder.hpp"
#include "generic/scopeexit.hpp"

#include "common.hpp"

#include "uv_callbacks.hpp"

namespace cr2
{

template <typename F, typename R, std::size_t S>
class uv_coroutine
{
  template <typename, typename, std::size_t>
  friend class uv_coroutine;

private:
  boost::context::fiber fi_;

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

  //
  void destroy()
    noexcept(std::is_nothrow_destructible_v<R>)
  {
    static_assert(
      !std::is_pointer_v<R> &&
      !std::is_reference_v<R> &&
      !std::is_same_v<detail::empty_t, R>
    );

    if (NEW != state_)
    {
      std::destroy_at(std::launder(reinterpret_cast<R*>(&r_)));
    }
  }

  template <enum state State>
  void suspend()
  {
    state_ = State;

    fi_ = std::move(fi_).resume();
  }

public:
  explicit uv_coroutine(F&& f)
    noexcept(noexcept(std::is_nothrow_move_constructible_v<F>)):
    state_{NEW},
    f_(std::move(f))
  {
    reset();
  }

  ~uv_coroutine()
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

  uv_coroutine(uv_coroutine const&) = delete;
  uv_coroutine(uv_coroutine&&) = default;

  explicit operator bool() const noexcept { return bool(state_); }

  void operator()()
  {
    state_ = RUNNING;

    fi_ = std::move(fi_).resume();
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
  void pause() { suspend<PAUSED>(); }

  void reset()
  {
    if constexpr(
      !std::is_pointer_v<R> &&
      !std::is_reference_v<R> &&
      !std::is_same_v<detail::empty_t, R>
    )
    {
      destroy();
    }

    fi_ = {
      std::allocator_arg_t{},
      boost::context::fixedsize_stack(S),
      [this](auto&& fi)
      {
        fi_ = std::move(fi);

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

        state_ = DEAD;

        return std::move(fi_);
      }
    };

    state_ = NEW;
  }

  void suspend() { return suspend<SUSPENDED>(); }

  template <typename A, typename B, std::size_t C>
  void suspend_to(uv_coroutine<A, B, C>& c)
  { // suspend means "out"
    c(); suspend();
  }

  template <auto G>
  auto await(uv_connect_t* const uvc, auto&& ...a) noexcept
  {
    int r;

    gnr::forwarder<void(int) noexcept> g(
      [&](auto const s) noexcept
      {
        r = s;
        state_ = SUSPENDED;
      }
    );

    uvc->data = &g;

    if (auto const r(G(uvc,
        std::forward<decltype(a)>(a)...,
        detail::uv::uv_connect_cb
      )
    ); r < 0)
    {
      return r;
    }

    pause();

    return r;
  }

  template <auto G>
  auto await(uv_fs_t* const uvfs, auto&& ...a) noexcept
  {
    gnr::forwarder<void() noexcept> g(
      [&]() noexcept
      {
        state_ = SUSPENDED;
      }
    );

    uvfs->data = &g;

    if (auto const r(G(uv_default_loop(),
        uvfs,
        std::forward<decltype(a)>(a)...,
        detail::uv::uv_fs_cb
      )
    ); r < 0)
    {
      return decltype(uvfs->result)(r);
    }

    pause();

    SCOPE_EXIT(uvfs, uv_fs_req_cleanup(uvfs));

    return uvfs->result;
  }

  template <auto G>
  auto await(uv_handle_t* const uvh) noexcept
    requires(G == uv_close)
  {
    gnr::forwarder<void() noexcept> g(
      [&]() noexcept
      {
        state_ = SUSPENDED;
      }
    );

    uvh->data = &g;

    G(uvh, detail::uv::uv_close_cb);

    pause();
  }

  template <auto G>
  auto await(uv_stream_t* const uvs) noexcept
    requires(G == uv_read_start)
  {
    ssize_t s;
    uv_buf_t const* b;

    gnr::forwarder<void(ssize_t, uv_buf_t const*) noexcept> g(
      [&](auto const sz, auto const buf) noexcept
      {
        s = sz;
        b = buf;
        state_ = SUSPENDED;
      }
    );

    char data[65536];

    auto t(std::pair<void*, char*>(&g, data));

    uvs->data = &t;

    if (s = G(uvs,
        detail::uv::uv_alloc_cb,
        detail::uv::uv_read_cb
    ); s < 0)
    {
      return std::pair{s, decltype(b){}};
    }

    pause();

    return std::pair{s, b};
  }
};

namespace uv
{

template <std::size_t S = default_stack_size>
auto make_plain(auto&& f)
  noexcept(noexcept(
      uv_coroutine<
        std::remove_cvref_t<decltype(f)>,
        detail::transform_void_t<
          decltype(
            std::declval<std::remove_cvref_t<decltype(f)>>()(
              std::declval<
                uv_coroutine<std::remove_cvref_t<decltype(f)>, detail::empty_t, S>&
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
        std::declval<uv_coroutine<F, detail::empty_t, S>&>()
      )
    )
  >;
  using C = uv_coroutine<F, R, S>;

  return C(std::forward<decltype(f)>(f));
}

template <std::size_t S = default_stack_size>
auto make_shared(auto&& f)
{
  using F = std::remove_cvref_t<decltype(f)>;
  using R = detail::transform_void_t<
    decltype(std::declval<F>()(
        std::declval<uv_coroutine<F, detail::empty_t, S>&>()
      )
    )
  >;
  using C = uv_coroutine<F, R, S>;

  return std::make_shared<C>(std::forward<decltype(f)>(f));
}

template <std::size_t S = default_stack_size>
auto make_unique(auto&& f)
{
  using F = std::remove_cvref_t<decltype(f)>;
  using R = detail::transform_void_t<
    decltype(std::declval<F>()(
        std::declval<uv_coroutine<F, detail::empty_t, S>&>()
      )
    )
  >;
  using C = uv_coroutine<F, R, S>;

  return std::make_unique<C>(std::forward<decltype(f)>(f));
}

auto run(auto&& ...c)
  noexcept(noexcept((c.template retval<>(), ...)))
  requires(sizeof...(c) >= 1)
{
  {
    for (bool p, s;;)
    {
      p = s = {};

      (
        (
          (c.state() >= NEW ? c() : void()),
          (s = s || (SUSPENDED == c.state()), p = p || (PAUSED == c.state()))
        ),
        ...
      );

      if (p || s)
      {
        uv_run(uv_default_loop(), s ? UV_RUN_NOWAIT : UV_RUN_ONCE);
      }
      else
      {
        break;
      }
    }
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

#endif // CR2_UV_COROUTINE_HPP
