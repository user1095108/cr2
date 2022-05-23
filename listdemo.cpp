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
        std::cout << 'a' << std::endl;
        c.suspend();
      }
    },
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

  l.reverse();

  for (auto const i: {0, 1, 2}) { l(); }

  return 0;
}
