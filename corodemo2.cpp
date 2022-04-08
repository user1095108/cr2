#include <iostream>

#include "coroutine.hpp"

int main()
{
  cr2::coroutine c0(
    [](auto& c)
    {
      for (;;)
      {
        std::cout << "coro0\n";
        c.suspend();
      }
    }
  );

  cr2::coroutine c1(
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
  );

  cr2::coroutine c2(
    [](auto& c)
    {
      c.suspend_on(EV_CLOSED|EV_READ, STDIN_FILENO);
      std::cout << "coro2\n";
    }
  );

  std::cout << std::get<0>(cr2::await(c1, c2)) << std::endl;

  return 0;
}
