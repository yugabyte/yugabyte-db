// Copyright (c) YugabyteDB, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.
//

#pragma once

#include <type_traits>

namespace yb {

namespace concepts::internal {

template <class, class>
struct InvocableAsHelper;

template <class T, class R, class... Args>
struct InvocableAsHelper<T, R(Args...)> {
  static constexpr auto value = std::is_invocable_r_v<R, T, Args...>;
};

template <template<class...> class T, class... Args>
void TemplateTypeHelper(const T<Args...>& t) {}

} // namespace concepts::internal

template <class T, class Func>
concept InvocableAs = concepts::internal::InvocableAsHelper<T, Func>::value;

template <class T, template<class...> class Tmpl>
concept TemplateType = requires (const T& t) { concepts::internal::TemplateTypeHelper<Tmpl>(t); };

template <class T, template <class...> class... Tmpls>
concept AnyOfTemplateTypes = (... || TemplateType<T, Tmpls>);

template <class T>
concept ContainerType = requires (const T& t) {
  typename T::value_type;
  typename T::iterator;
  typename T::const_iterator;
  t.begin();
  t.end();
  t.cbegin();
  t.cend();
  t.empty();
};

template <class Container, class T>
concept ContainerOf = ContainerType<Container> && std::same_as<typename Container::value_type, T>;

} // namespace yb
