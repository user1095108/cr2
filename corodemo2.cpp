#include <iostream>

#include "coroutine.hpp"

int main()
{
  auto const base(event_base_new());

  {
    auto c0(cr2::make(
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

    auto c1(cr2::make(
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
        }
      )
    );

    c1();

    std::cout << cr2::retval(c1) << std::endl;
  }

  auto c2(cr2::make(
      [&](auto& c)
      {
        evutil_make_socket_nonblocking(STDIN_FILENO);
        c.suspend_on(base, STDIN_FILENO, EV_READ);

        std::cout << "coro2\n";
      }
    )
  );

  for (c2(); c2;)
  {
    event_base_loop(base, EVLOOP_NONBLOCK);
  }

  return 0;
}
