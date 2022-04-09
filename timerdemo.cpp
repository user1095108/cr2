#include <iostream>

#include "coroutine.hpp"

int main()
{
  std::cout <<
    std::get<2>(
      cr2::make_and_run<128, 128, 128>(
        [](auto& c)
        {
          unsigned i(1);

          do
          {
            std::cout << "coro0 " << i++ << '\n';
            c.sleep(std::chrono::seconds(1));
          }
          while (10 != i);
        },
        [](auto& c)
        {
          unsigned i(9);

          do
          {
            std::cout << "coro1 " << i-- << '\n';
            c.sleep(std::chrono::seconds(1));
          }
          while (i);
        },
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
    ) <<
    std::endl;

  cr2::make_and_run<128>(
    [&](auto& c)
    {
      do
      {
        std::cout << "waiting for keypress\n";
      }
      while (!(EV_READ & std::get<0>(
        c.suspend_on(std::chrono::seconds(1), EV_READ, STDIN_FILENO))));
    }
  );

  return 0;
}
