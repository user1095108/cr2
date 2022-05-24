#include <iostream>
#include <thread>

#include "basic_coroutine.hpp"
//#include "portable_coroutine.hpp"
#include "libevent_support.hpp"

using namespace cr2::literals;
using namespace std::literals::chrono_literals;

int main()
{
  evthread_use_pthreads();

  std::cout <<
    std::get<2>(
      cr2::make_and_run<128_k, 128_k, 128_k>(
        [](auto& c)
        {
          unsigned i(1);

          do
          {
            std::cout << "coro0 " << i++ << '\n';
            cr2::await(c, 1s);
          }
          while (10 != i);
        },
        [](auto& c)
        {
          unsigned i(9);

          do
          {
            std::cout << "coro1 " << i-- << '\n';
            cr2::await(c, 1s);
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

  cr2::make_and_run<128_k>(
    [](auto& c)
    {
      struct myevent : event
      {
        float x, y;
      } e;

      std::thread(
        [&]() noexcept
        {
          e.x = 1.f; e.y = 2.f;
          evuser_trigger(&e); // race condition, we hope e is initialized
        }
      ).detach();

      cr2::await(c, &e);

      std::cout << e.x << ' ' << e.y << '\n';

      do
      {
        std::cout << "waiting for keypress\n";
      }
      while (!(EV_READ &
        std::get<0>(cr2::await(c, 1s, EV_READ, STDIN_FILENO))));
    }
  );

  event_base_free(cr2::base);
  libevent_global_shutdown();

  return 0;
}
