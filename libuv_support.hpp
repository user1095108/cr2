#ifndef CR2_LIBUV_SUPPORT_HPP
# define CR2_LIBUV_SUPPORT_HPP
# pragma once

#include <uv.h>

#include "common2.hpp"

namespace cr2
{

extern "C"
{

inline void uv_alloc_cb(uv_handle_t* const uvh, std::size_t,
  uv_buf_t* const buf) noexcept
{
  auto const p(static_cast<std::pair<void*, char*>*>(uvh->data));

  buf->base = std::get<1>(*p);
  buf->len = 65536;
}

inline void uv_close_cb(uv_handle_t* const uvh) noexcept
{
  (*static_cast<gnr::forwarder<void()>*>(uvh->data))();
}

inline void uv_fs_cb(uv_fs_t* const uvfs) noexcept
{
  (*static_cast<gnr::forwarder<void()>*>(uvfs->data))();
}

inline void uv_read_cb(uv_stream_t* const uvs,
  ssize_t const sz, uv_buf_t const* const buf) noexcept
{
  auto const p(static_cast<std::pair<void*, char*>*>(uvs->data));

  (*static_cast<gnr::forwarder<void(ssize_t, uv_buf_t const*)>*>(
    std::get<0>(*p)))(sz, buf);
}

inline void uv_connect_cb(uv_connect_t* const uvc, int const status) noexcept
{
  (*static_cast<gnr::forwarder<void(int)>*>(uvc->data))(status);
}

}

template <auto G>
auto await(auto& c, uv_connect_t* const uvc, auto&& ...a)
  noexcept(noexcept(c.pause()))
{
  int r;

  gnr::forwarder<void(int) noexcept> g(
    [&](auto const s) noexcept
    {
      r = s;
      c.unpause();
    }
  );

  uvc->data = &g;

  if (auto const r(G(uvc,
      std::forward<decltype(a)>(a)...,
      uv_connect_cb
    )
  ); r < 0)
  {
    return r;
  }

  c.pause();

  return r;
}

template <auto G>
auto await(auto& c, uv_fs_t* const uvfs, auto&& ...a)
  noexcept(noexcept(c.pause()))
{
  gnr::forwarder<void() noexcept> g([&]() noexcept { c.unpause(); });

  uvfs->data = &g;

  if (auto const r(G(uv_default_loop(),
      uvfs,
      std::forward<decltype(a)>(a)...,
      uv_fs_cb
    )
  ); r < 0)
  {
    return decltype(uvfs->result)(r);
  }

  c.pause();

  SCOPE_EXIT(uvfs, uv_fs_req_cleanup(uvfs));

  return uvfs->result;
}

template <auto G>
auto await(auto& c, uv_handle_t* const uvh)
  noexcept(noexcept(c.pause()))
  requires(G == uv_close)
{
  gnr::forwarder<void() noexcept> g([&]() noexcept { c.unpause(); });

  uvh->data = &g;

  G(uvh, uv_close_cb);

  c.pause();
}

template <auto G>
auto await(auto& c, uv_stream_t* const uvs, char* const data)
  noexcept(noexcept(c.pause()))
  requires(G == uv_read_start)
{
  ssize_t s;
  uv_buf_t const* b;

  gnr::forwarder<void(ssize_t, uv_buf_t const*) noexcept> g(
    [&](auto const sz, auto const buf) noexcept
    {
      s = sz;
      b = buf;

      c.unpause();
    }
  );

  std::pair<void*, char*> p(&g, data);

  uvs->data = &p;

  if (s = G(uvs,
      uv_alloc_cb,
      uv_read_cb
  ); s < 0)
  {
    return std::pair{s, decltype(b){}};
  }

  c.pause();

  return std::pair{s, b};
}

auto run(auto&& ...c)
  noexcept(noexcept((c.template retval<>(), ...)))
  requires(sizeof...(c) >= 1)
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

#endif // CR2_LIBUV_SUPPORT_HPP
