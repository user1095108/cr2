#include <iostream>

#include "coroutine.hpp"

using namespace cr2::literals;

struct A
{
  ~A()
  {
    std::cout << "destroyed\n";
  }
};

int main()
{
  auto c0(cr2::make_plain<128_k>(
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

  cr2::make_and_run<128_k, 128_k>(
    [&](auto& c)
    {
      A a;

      for (int i{}; i != 3; ++i)
      {
        std::cout << i << '\n';

        c.suspend_to(c0);
      }
    },
    [](auto& c)
    {
      c.await(EV_READ, STDIN_FILENO);
      std::cout << "coro2\n";
    }
  );

  event_base_free(cr2::base);
  libevent_global_shutdown();

  return 0;
}
