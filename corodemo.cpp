#include <iostream>

#include "coroutine.hpp"

struct A
{
  ~A()
  {
    std::cout << "destroyed\n";
  }
};

int main()
{
  auto c0(cr2::make_coroutine<512>(
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

  cr2::await(
    cr2::make_coroutine(
      [&](auto& c)
      {
        A a;

        for (int i{}; i != 3; ++i)
        {
          std::cout << i << '\n';

          c.suspend_to(c0);
        }
      }
    ),
    cr2::make_coroutine<256>(
      [](auto& c)
      {
        c.suspend_on(EV_READ, STDIN_FILENO);
        std::cout << "coro2\n";
      }
    )
  );

  return 0;
}
