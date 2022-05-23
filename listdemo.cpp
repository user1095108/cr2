#include <iostream>

#include <ranges>

#include "basic_coroutine.hpp"
//#include "portable_coroutine.hpp"
#include "libnone_support.hpp"

#include "list.hpp"

int main()
{
  cr2::list l{
    [](auto& c)
    {
      for (;;)
      {
        std::cout << 'b' << std::endl;
        c.suspend();
      }
    },
    [](auto& c)
    {
      for (;;)
      {
        std::cout << 'c' << std::endl;
        c.suspend();
      }
    }
  };

  l.emplace_front(
    [](auto& c)
    {
      for (;;)
      {
        std::cout << 'a' << std::endl;
        c.suspend();
      }
    }
  );

  l.push_back(
    [](auto& c)
    {
      std::intmax_t j(6);

      for (auto i(j - 1); 1 != i; --i)
      {
        std::cout << "coro\n";

        j *= i;
        c.suspend();
      }

      std::cout << j << std::endl;
    }
  );

  l.reverse();

  while (l) l();

  return 0;
}
