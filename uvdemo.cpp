#include <iostream>

#include "basic_coroutine.hpp"
//#include "portable_coroutine.hpp"
#include "libuv_support.hpp"

using namespace cr2::literals;

int main()
{
  std::cout <<
    std::get<1>(
      cr2::make_and_run<128_k, 128_k>(
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

          {
            char data[64_k];

            auto const buf(uv_buf_init(data, sizeof(data)));

            uv_fs_t uvfs;

            auto const uvf(cr2::await<uv_fs_open>(c, &uvfs,
              "uvdemo.cpp", 0, O_RDONLY));

            for (std::uintmax_t off{};;)
            {
              if (auto const sz(cr2::await<uv_fs_read>(c, &uvfs, uvf,
                &buf, 1, off)); sz > 0)
              {
                off += sz;
                r.append(data, sz);
              }
              else
              {
                break;
              }
            }

            cr2::await<uv_fs_close>(c, &uvfs, uvf);
          }

          return r;
        }
      )
    ) <<
    std::endl;

  uv_loop_close(uv_default_loop());

  return 0;
}
