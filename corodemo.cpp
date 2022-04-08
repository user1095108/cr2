#include <iostream>

#include "coroutine.hpp"

struct A
{
  ~A()
  {
    std::cout << "destroyed" << std::endl;
  }
};

int main()
{
  cr2::coroutine c0(
    [](auto& c)
    {
      for (;;)
      {
        std::cout << "hi!\n";
        c.suspend();
      }
    }
  );

  cr2::coroutine c1(
    [&](auto& c)
    {
      c.suspend_to(c0);

      A a;

      for (int i{}; i != 3; ++i)
      {
        std::cout << i << std::endl;

        c.suspend();
      }

      c.suspend_to(c0);
    }
  );

  cr2::coroutine c2(
    [](auto& c)
    {
      c.suspend_on(EV_READ, STDIN_FILENO);
      std::cout << "coro2\n";
    }
  );

  cr2::await(c1, c2);

  return 0;
}
