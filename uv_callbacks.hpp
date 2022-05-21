#ifndef CR2_UVCALLBACKS_HPP
# define CR2_UVCALLBACKS_HPP
# pragma once

#include "generic/forwarder.hpp"

namespace cr2::detail::uv
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

}

#endif // CR2_UVCALLBACKS_HPP
