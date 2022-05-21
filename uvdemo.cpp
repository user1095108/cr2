#include <iostream>

#include "uv_coroutine.hpp"

using namespace cr2::literals;

int main()
{
  std::cout <<
    std::get<1>(
      cr2::uv::make_and_run<128_k, 128_k>(
        [&](auto& c)
        {
          std::intmax_t j(5);

          for (auto i(j - 1); 1 != i; --i)
          {
            std::cout << "coro1\n";

            j *= i;
            c.suspend();
          }

          return j;
        },
        [](auto& c)
        {
          std::string r;

          char data[1024];

          auto const buf(uv_buf_init(data, sizeof(data)));

          uv_fs_t fs;

          auto const fh(c.await(uv_fs_open, &fs, "uvdemo.cpp", 0, O_RDONLY));

          for (std::intmax_t off{};;)
          {
            if (auto const sz(c.await(uv_fs_read, &fs, fh, &buf, 1,
              off)); sz > 0)
            {
              off += sz;
              r.append(data, sz);
            }
            else
            {
              break;
            }
          }

          c.await(uv_fs_close, &fs, fh);

          return r;
        }
      )
    ) <<
    std::endl;

  return 0;
}
