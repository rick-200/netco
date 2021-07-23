#pragma once
#include <functional>
#include <tuple>
#include <type_traits>

#include "netco.h"
namespace netco_details {
template <typename TFunc, typename Ttuple, size_t... Idx>
inline auto _tuple_invoke_impl(TFunc&& f, Ttuple&& t, std::index_sequence<Idx...>&&) {
  return std::invoke(f, std::get<Idx>(t)...);
}
template <typename TFunc, typename Ttuple>
inline auto tuple_invoke(TFunc&& f, Ttuple&& t) {
  return _tuple_invoke_impl(std::forward<TFunc>(f), std::forward<Ttuple>(t), std::make_index_sequence<std::tuple_size_v<Ttuple>>{});
}

template <typename TFunc, typename Ttuple>
struct wrapper_plus_param {
  TFunc f;
  Ttuple t;
};
template <typename TFunc, typename Ttuple>
inline void co_wrapper_plus(void* param) {
  wrapper_plus_param<TFunc, Ttuple>* p = reinterpret_cast<wrapper_plus_param<TFunc, Ttuple>*>(param);
  TFunc f{std::move(p->f)};
  Ttuple t{std::move(p->t)};
  delete p;
  tuple_invoke(std::move(f), std::move(t));
}
}  // namespace netco_details
template <typename TFunc, typename... TArgs>
inline co_task co_create_ex(TFunc&& f, TArgs&&... args) {
  using Ttuple = decltype(std::make_tuple(std::forward<TArgs>(args)...));
  void* p = new netco_details::wrapper_plus_param<TFunc, Ttuple>{std::move(f), std::make_tuple(std::forward<TArgs>(args)...)};
  return co_create(&netco_details::co_wrapper_plus<TFunc, Ttuple>, p);
}
