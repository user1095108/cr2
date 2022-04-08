#include <iostream>

#include "coroutine.hpp"

int main()
{
  auto c0(cr2::make_coroutine(
      [](auto& c) -> void
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
      [&](auto& c) -> std::intmax_t
      {
        std::intmax_t j(5);

        for (auto i(j - 1); 1 != i; --i)
        {
          std::cout << "coro1\n";

          j *= i;
          c.suspend();
        }

        return j;
      }
    )
  );

  auto c2(cr2::make_coroutine(
      [](auto& c) -> void
      {
        c.suspend_on(EV_READ, STDIN_FILENO);
        std::cout << "coro2\n";
      }
    )
  );

  std::cout << std::get<0>(cr2::await(c1, c2)) << std::endl;

  return 0;
}
