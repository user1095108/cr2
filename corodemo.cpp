#include <iostream>

#include "basic_coroutine.hpp"

using namespace cr2::literals;

struct A
{
  ~A()
  {
    std::cout << "destroyed\n";
  }
};

int main()
{
  auto c0(cr2::basic::make_plain<128_k>(
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

  std::cout << std::get<1>(
    cr2::basic::make_and_run<128_k, 128_k>(
      [&](auto& c)
      {
        A a;

        for (int i{}; i != 3; ++i)
        {
          std::cout << i << '\n';

          c.suspend_to(c0);
        }
      },
      [](auto& c)
      {
        std::intmax_t j(5);

        for (auto i(j - 1); 1 != i; --i)
        {
          std::cout << "coro2\n";

          j *= i;
          c.suspend();
        }

        return j;
      }
    )
  ) << '\n';

  return 0;
}
