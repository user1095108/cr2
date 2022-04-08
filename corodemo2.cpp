#include <iostream>

#include "coroutine.hpp"

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

  std::cout <<
    std::get<0>(
      cr2::run(
        cr2::make_coroutine(
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
        ),
        cr2::make_coroutine(
          [](auto& c)
          {
            c.suspend_on(EV_READ, STDIN_FILENO);
            std::cout << "coro2\n";
          }
        )
      )
    ) <<
    std::endl;

  return 0;
}
