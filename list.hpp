#ifndef CR2_LIST_HPP
# define CR2_LIST_HPP
# pragma once

#include <memory>

#include "xl/list.hpp"

namespace cr2
{

namespace detail
{

struct control
{
  void* id;

  void (*invoke)(void*) noexcept;
  enum state (*state)(void*) noexcept;
  void (*reset)(void*);
  void (*destroy)(void*);

  std::unique_ptr<char[]> store;

  control(auto&& l):
    store(
      ::new char[sizeof(decltype(make_plain(std::forward<decltype(l)>(l))))]
    )
  {
    using C = decltype(make_plain(std::forward<decltype(l)>(l)));

    id = ::new (store.get()) C(
        make_plain(std::forward<decltype(l)>(l))
      );

    invoke = [](void* const p) noexcept
      {
        (*static_cast<C*>(p))();
      };

    state = [](void* const p) noexcept
      {
        return static_cast<C*>(p)->state();
      };

    reset = [](void* const p) { static_cast<C*>(p)->reset(); };
    destroy = [](void* const p) { static_cast<C*>(p)->~C(); };
  }

  control(control&&) = default;

  ~control() { destroy(id); }

  //
  control& operator=(control const&) = delete;
  control& operator=(control&&) = delete;
};

}

class list: public xl::list<detail::control>
{
public:
  explicit list(auto&& ...f)
    noexcept(noexcept(
        (
          emplace_back(std::forward<decltype(f)>(f)),
          ...
        )
      )
    )
  {
    (
      emplace_back(std::forward<decltype(f)>(f)),
      ...
    );
  }

  //
  explicit operator bool() const noexcept
  {
    return std::all_of(
      begin(),
      end(),
      [](auto&& e) noexcept { return e.state(e.id); }
    );
  }

  void operator()() const noexcept
  {
    std::for_each(
      begin(),
      end(),
      [](auto&& e) noexcept { e.invoke(e.id); }
    );
  }

  //
  bool all(enum state const s) const noexcept
  {
    return std::all_of(
      begin(),
      end(),
      [s](auto&& e) noexcept { return s == e.state(e.id); }
    );
  }

  bool any(enum state const s) const noexcept
  {
    return std::any_of(
      begin(),
      end(),
      [s](auto&& e) noexcept { return s == e.state(e.id); }
    );
  }

  bool none(enum state const s) const noexcept
  {
    return std::none_of(
      begin(),
      end(),
      [s](auto&& e) noexcept { return s == e.state(e.id); }
    );
  }

  //
  void assign(auto&& ...f)
    noexcept(
      noexcept(clear()) &&
      noexcept(
        (
          emplace_back(std::forward<decltype(f)>(f)),
          ...
        )
      )
    )
  {
    clear();

    (
      emplace_back(std::forward<decltype(f)>(f)),
      ...
    );
  }

  //
  void reset()
  {
    std::for_each(
      begin(),
      end(),
      [](auto&& e) { e.reset(e.id); }
    );
  }
};

}

#endif // CR2_LIST_HPP
