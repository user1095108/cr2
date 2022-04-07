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
  auto c(
    cr2::make_coroutine(
      [](auto& c)
      {
        A a;

        for (int i{}; i != 3; ++i)
        {
          std::cout << i << std::endl;

          c.suspend();
        }
      }
    )
  );

  while (c)
  {
    std::cout << "resuming" << std::endl;
    c.resume();
  }

  std::cin.ignore();

  return 0;
}
