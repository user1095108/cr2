#include <iostream>

#include <ranges>

//#include "basic_coroutine.hpp"
#include "portable_coroutine.hpp"
#include "libnone_support.hpp"

#include "list.hpp"

using namespace cr2::literals;

int main()
{
  cr2::list l{
    cr2::make_plain<128_k>(
      [](auto& c)
      {
        for (;;)
        {
          std::cout << 'b' << std::endl;
          c.suspend();
        }
      }
    ),
    cr2::make_plain<128_k>(
      [](auto& c)
      {
        for (;;)
        {
          std::cout << 'c' << std::endl;
          c.suspend();
        }
      }
    ),
  };

  l.push_back(
    cr2::make_plain<128_k>(
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
    )
  );

  l.emplace_front(
    cr2::make_plain<128_k>(
      [](auto& c)
      {
        for (;;)
        {
          std::cout << 'a' << std::endl;
          c.suspend();
        }
      }
    )
  );

  while (
    std::all_of(
      l.cbegin(),
      l.cend(),
      [](auto&& e) noexcept { return e.state(); }
    )
  )
  {
    l();
  }

  return 0;
}
