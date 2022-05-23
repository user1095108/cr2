#ifndef CR2_LIST_HPP
# define CR2_LIST_HPP
# pragma once

#include <algorithm>
#include <any>

#include "generic/forwarder.hpp"

#include "xl/list.hpp"

namespace cr2
{

namespace detail
{

struct control
{
  void* id;

  void (*invoke)(void*) noexcept;
  void (*reset)(void*);
  enum state (*state)(void*) noexcept;

  std::any store;
};

inline void set_control(control& ctrl, auto&& l)
{
  ctrl.store = make_shared(std::forward<decltype(l)>(l));

  auto& c(
    *std::any_cast<
      std::remove_cvref_t<
        decltype(make_shared(std::forward<decltype(l)>(l)))
      >&
    >(ctrl.store)
  );

  ctrl.id = &c;

  ctrl.invoke = [](void* const p) noexcept
    {
      (*static_cast<decltype(&c)>(p))();
    };

  ctrl.reset = [](void* const p)
    {
      static_cast<decltype(&c)>(p)->reset();
    };

  ctrl.state = [](void* const p) noexcept
    {
      return static_cast<decltype(&c)>(p)->state();
    };
}

}

class list: private xl::list<detail::control>
{
  using inherited_t = xl::list<detail::control>;

public:
  using inherited_t::value_type;

  using inherited_t::difference_type;
  using inherited_t::size_type;
  using inherited_t::reference;
  using inherited_t::const_reference;

  using inherited_t::iterator;
  using inherited_t::reverse_iterator;
  using inherited_t::const_iterator;
  using inherited_t::const_reverse_iterator;

public:
  list() = default;

  list(auto&& ...f)
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
      [&](auto&& e) noexcept { return e.state(e.id); }
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
  using inherited_t::empty;

  //
  using inherited_t::begin;
  using inherited_t::end;

  using inherited_t::cbegin;
  using inherited_t::cend;

  using inherited_t::rbegin;
  using inherited_t::rend;

  using inherited_t::crbegin;
  using inherited_t::crend;

  //
  using inherited_t::operator[];

  //
  using inherited_t::at;

  using inherited_t::back;
  using inherited_t::front;

  using inherited_t::size;

  //
  void assign(auto&& ...f)
    noexcept(
      noexcept(inherited_t::clear()) ||
      noexcept(
        (
          emplace_back(std::forward<decltype(f)>(f)),
          ...
        )
      )
    )
  {
    inherited_t::clear();

    (
      emplace_back(std::forward<decltype(f)>(f)),
      ...
    );
  }

  //
  void emplace_back(auto&& l)
    noexcept(noexcept(inherited_t::emplace_back()))
  {
    inherited_t::emplace_back();

    detail::set_control(back(), std::forward<decltype(l)>(l));
  }

  void emplace_front(auto&& l)
    noexcept(noexcept(inherited_t::emplace_front()))
  {
    inherited_t::emplace_front();

    detail::set_control(front(), std::forward<decltype(l)>(l));
  }

  //
  using inherited_t::pop_front;
  using inherited_t::pop_back;

  //
  using inherited_t::clear;
  using inherited_t::erase;

  //
  using inherited_t::reverse;

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
