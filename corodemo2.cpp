#include <iostream>

#include "event_coroutine.hpp"

using namespace cr2::literals;

int main()
{
  auto c0(cr2::event::make_plain(
      [](auto& c)
      {
        for (;;)
        {
          std::cout << "coro0\n";
          c.suspend();
        }
      }
    )
  );

  std::cout <<
    std::get<0>(
      cr2::event::make_and_run<128_k, 128_k>(
        [&](auto& c)
        {
          std::intmax_t j(5);

          for (auto i(j - 1); 1 != i; --i)
          {
            std::cout << "coro1\n";

            j *= i;
            c.suspend_to(c0);
          }

          return j;
        },
        [](auto& c)
        {
          c.await(EV_READ, STDIN_FILENO);
          std::cout << "coro2\n";
        }
      )
    ) <<
    std::endl;

  event_base_free(cr2::base);
  libevent_global_shutdown();

  return 0;
}
