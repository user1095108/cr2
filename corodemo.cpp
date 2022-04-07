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
  auto c0(cr2::make(
      [](auto& c)
      {
        for (;;)
        {
          std::cout << "hi!\n";
          c.suspend();
        }
      }
    )
  );

  auto c1(cr2::make(
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
    )
  );

  for (c1(); c1;)
  {
    std::cout << "resuming" << std::endl;
    c1();
  }

  std::cin.ignore();

  return 0;
}
