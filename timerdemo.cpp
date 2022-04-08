#include <iostream>

#include "coroutine.hpp"

int main()
{
  std::cout <<
    std::get<2>(
      cr2::await(
        cr2::make_coroutine<128>(
          [](auto& c)
          {
            unsigned i(1);

            do
            {
              std::cout << "coro0 " << i++ << '\n';
              c.sleep(std::chrono::seconds(1));
            }
            while (10 != i);
          }
        ),
        cr2::make_coroutine<128>(
          [](auto& c)
          {
            unsigned i(9);

            do
            {
              std::cout << "coro1 " << i-- << '\n';
              c.sleep(std::chrono::seconds(1));
            }
            while (i);
          }
        ),
        cr2::make_coroutine<128>(
          [](auto& c)
          {
            std::intmax_t j(5);

            for (auto i(j - 1); 1 != i; --i)
            {
              j *= i;
              c.suspend();
            }

            return j;
          }
        )
      )
    ) <<
    std::endl;

  return 0;
}
