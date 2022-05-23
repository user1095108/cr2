#ifndef CR2_LIST_HPP
# define CR2_LIST_HPP
# pragma once

#include <memory>

#include "xl/list.hpp"

namespace cr2
{

class list;

namespace detail
{

class control
{
  friend class ::cr2::list;

private:
  void* id_;

  void (*invoke_)(void*) noexcept;
  enum state (*state_)(void*) noexcept;
  void (*reset_)(void*);
  void (*destroy_)(void*);

  std::unique_ptr<char[]> store_;

public:
  control(auto&& c):
    store_(::new char[sizeof(c)])
  {
    using C = std::remove_reference_t<decltype(c)>;
    id_ = ::new (store_.get()) C(std::move(c));

    invoke_ = [](void* const p) noexcept {(*std::launder(reinterpret_cast<C*>(p)))();};
    state_ = [](void* const p) noexcept {return std::launder(reinterpret_cast<C*>(p))->state();};
    reset_ = [](void* const p) {std::launder(reinterpret_cast<C*>(p))->reset();};
    destroy_ = [](void* const p) {std::launder(reinterpret_cast<C*>(p))->~C();};
  }

  control(control const&) = delete;
  control(control&& o) = default;

  ~control() { destroy_(id_); }

  //
  control& operator=(control const&) = delete;
  control& operator=(control&&) = delete;

  //
  void const* id() const noexcept { return id_; }

  enum state state() const noexcept { return state_(id_); }

  void reset() const { reset_(id_); }
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
    return std::any_of(
      begin(),
      end(),
      [](auto&& e) noexcept { return e.state(); }
    );
  }

  void operator()() const noexcept
  {
    std::for_each(
      begin(),
      end(),
      [](auto&& e) noexcept
      {
        if (e.state())
        {
          e.invoke_(e.id_);
        }
      }
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
  void reset() const
  {
    std::for_each(
      begin(),
      end(),
      [](auto& e) { e.reset(); }
    );
  }
};

}

#endif // CR2_LIST_HPP
