#include <iostream>

#include "curl/curl.h"

#include "coroutine.hpp"

namespace curl
{

extern "C" std::size_t c_get_write(char const* const buffer, std::size_t,
  std::size_t const nmemb, void* const ptr)
{
  return static_cast<gnr::forwarder<std::size_t(char const*, std::size_t)>*>(
    ptr)->operator()(buffer, nmemb);
}

auto get(auto& c, std::string_view const& url)
{
  auto const h(curl_easy_init());

  curl_easy_setopt(h, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(h, CURLOPT_HTTPGET, 1L);
  curl_easy_setopt(h, CURLOPT_NOPROGRESS, 1L);
  curl_easy_setopt(h, CURLOPT_TCP_FASTOPEN, 1L);
  curl_easy_setopt(h, CURLOPT_URL, url.data());
  //curl_easy_setopt(h, CURLOPT_VERBOSE, 0L);

  std::string s;

  gnr::forwarder<std::size_t(char const*, std::size_t)> f(
    [&](char const* const buffer, std::size_t const sz)
    {
      return s.append(buffer, sz), c.suspend(), sz;
    }
  );

  curl_easy_setopt(h, CURLOPT_WRITEDATA, &f);
  curl_easy_setopt(h, CURLOPT_WRITEFUNCTION, c_get_write);

  c.suspend();
  curl_easy_perform(h);

  curl_easy_cleanup(h);

  return s;
}

}

int main()
{
  curl_global_init(CURL_GLOBAL_DEFAULT);

  auto const t(
    cr2::make_and_run(
      [](auto& c)
      {
        std::intmax_t j(5);

        for (auto i(j - 1); 1 != i; --i)
        {
          std::cout << "coro0\n";

          j *= i;
          c.suspend();
        }

        return j;
      },
      [](auto& c)
      {
        return curl::get(c, "http://www.google.com/");
      },
      [](auto& c)
      {
        return curl::get(c, "http://www.cnn.com/");
      }
    )
  );

  std::cout << std::get<1>(t) << std::endl;
  std::cout << std::get<2>(t) << std::endl;
  std::cout << std::get<0>(t) << std::endl;

  curl_global_cleanup();

  return 0;
}
