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
  auto c0(cr2::make_coroutine(
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

  auto c1(cr2::make_coroutine(
      [&](auto& c)
      {
        A a;

        for (int i{}; i != 3; ++i)
        {
          std::cout << i << '\n';

          c.suspend_to(c0);
        }
      }
    )
  );

  auto c2(cr2::make_coroutine(
      [](auto& c)
      {
        c.suspend_on(EV_READ, STDIN_FILENO);
        std::cout << "coro2\n";
      }
    )
  );

  cr2::await(c1, c2);

  return 0;
}
