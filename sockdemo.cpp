#include <cstring>
#include <iostream>

#include <arpa/inet.h>
#include <netinet/in.h>

#include "coroutine.hpp"

using namespace std::literals::string_literals;

int main()
{
  auto const base(event_base_new());

  auto c(cr2::make(
      [&](auto& c)
      {
        evutil_socket_t sck;

        if (-1 == (sck = socket(AF_INET, SOCK_STREAM, IPPROTO_IP)))
        {
          return "socket()"s;
        }

        SCOPE_EXIT(&, evutil_closesocket(sck));

        evutil_make_socket_nonblocking(sck);

        {
          struct sockaddr_in sin;
          sin.sin_family = AF_INET;
          sin.sin_port = htons(7777);

          if (-1 == inet_pton(AF_INET, "127.0.0.1", &sin.sin_addr))
          {
            return "inet_pton()"s;
          }

          if (-1 == connect(sck, reinterpret_cast<sockaddr*>(&sin),
            sizeof(sin)))
          {
            if (EINPROGRESS != errno)
            {
              return "connect()"s;
            }
          }
        }

        std::string s;

        for (char buf[128];;)
        {
          std::cout << "coro1\n";

          if (int sz; -1 == (sz = recv(sck, buf, sizeof(buf), 0)))
          {
            if ((EAGAIN == errno) || (EWOULDBLOCK == errno))
            {
              std::cout << "pausing\n";
              c.suspend_on(base, sck, EV_CLOSED|EV_READ);

              continue;
            }
            else
            {
              return "recv(): "s + std::strerror(errno);
            }
          }
          else if (sz)
          {
            s.append(buf, sz);
          }
          else
          {
            break;
          }
        }

        return s;
      }
    )
  );

  for (c(); c;)
  {
    if (cr2::SUSPENDED != c.state())
    {
      std::cout << c.state() << std::endl;
    }

    event_base_loop(base, EVLOOP_NONBLOCK);
  }

  std::cout << cr2::retval(c) << std::endl;

  return 0;
}
