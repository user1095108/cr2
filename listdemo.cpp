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
        std::cout << '1' << std::endl;
        c.suspend();
      }
    },
    [](auto& c)
    {
      for (;;)
      {
        std::cout << '2' << std::endl;
        c.suspend();
      }
    },
    [](auto& c)
    {
      for (;;)
      {
        std::cout << '3' << std::endl;
        c.suspend();
      }
    }
  };

  l.reverse();

  for (auto const i: {0, 1, 2}) { l(); }

  return 0;
}
