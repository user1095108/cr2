#include <iostream>

#include <future>
#include <thread>

#include "coroutine.hpp"

using namespace std::literals::chrono_literals;

int main()
{
  evthread_use_pthreads();

  std::cout <<
    std::get<2>(
      cr2::make_and_run<128, 128, 128>(
        [](auto& c)
        {
          unsigned i(1);

          do
          {
            std::cout << "coro0 " << i++ << '\n';
            c.await(1s);
          }
          while (10 != i);
        },
        [](auto& c)
        {
          unsigned i(9);

          do
          {
            std::cout << "coro1 " << i-- << '\n';
            c.await(1s);
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
      struct event e;

      c.await(
        [&]()
        {
          std::thread([&]{evuser_trigger(&e);}).detach();
        },
        e
      );

      do
      {
        std::cout << "waiting for keypress\n";
      }
      while (!(EV_READ & std::get<0>(c.await(1s, EV_READ, STDIN_FILENO))));
    }
  );

  event_base_free(cr2::base);
  libevent_global_shutdown();

  return 0;
}
