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
        [](auto& c)
        {
          std::uintmax_t j(5);

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
            struct sockaddr_in addr;
            uv_ip4_addr("127.0.0.1", 7777, &addr);

            uv_tcp_t client;
            uv_tcp_init(uv_default_loop(), &client);

            uv_connect_t req;

            if (auto const e(cr2::await<uv_tcp_connect>(
                c,
                &req,
                &client,
                reinterpret_cast<struct sockaddr*>(&addr)
              )
            ); e >= 0)
            {
              for (char data[64_k];;)
              {
                if (auto const [sz, buf](
                  cr2::await<uv_read_start>(
                    c,
                    (uv_stream_t*)&client,
                    data
                  )
                ); sz >= 0)
                {
                  r.append(buf->base, sz);
                }
                else
                {
                  break;
                }
              }
            }

            uv_read_stop((uv_stream_t*)&client);

            cr2::await<uv_close>(c, (uv_handle_t*)&client);
          }

          return r;
        }
      )
    ) <<
  std::endl;

  uv_loop_close(uv_default_loop());

  return 0;
}
