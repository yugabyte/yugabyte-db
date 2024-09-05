// Copyright (c) YugaByteDB, Inc.
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

#include "yb/gutil/macros.h"

namespace yb {

template<class T>
class LightweightFunction;

// std::function like interface which helps to avoid dynamic memory allocations
// in case of using short lived callbacks created from lambda.
// LightweightFunction objects is not intended to be stored for a long time.
template<class Ret, class... Args>
class LightweightFunction<Ret(Args...)> {
 public:
  Ret operator()(Args... args) const {
    return Call(std::forward<decltype(args)>(args)...);
  }

 protected:
  LightweightFunction() = default;
  virtual ~LightweightFunction() = default;

 private:
  virtual Ret Call(Args... args) const = 0;

  DISALLOW_COPY_AND_ASSIGN(LightweightFunction);
};

template<class T>
using LWFunction = LightweightFunction<T>;

namespace lw_function_details {

struct TransparentReferenceTransformer {
  template<class T>
  static const T& Transform(const T& t) {
    return t;
  }
};

struct MutableReferenceTransformer {
  template<class T>
  static T& Transform(const T& t) {
    return const_cast<T&>(t);
  }
};

// Implementation of LightweightFunction interface which calls functor.
// Note: LightweightFunctionImpl object doesn't own the functor so it caller code responsibility
// to guarantee that functor is alive for the whole lifetime of LightweightFunctionImpl object.
template<class Trans, class Func, class Ret, class... Args>
class LightweightFunctionImpl : public LWFunction<Ret(Args...)> {
 public:
  explicit LightweightFunctionImpl(const Func& functor)
      : functor_(functor) {
  }

 private:
  Ret Call(Args... args) const override {
    return Trans::Transform(functor_)(std::forward<decltype(args)>(args)...);
  }

  const Func& functor_;
};

template<class T1, class T2>
struct TypeDeducer;

template<class Func, class Ret, class... Args>
struct TypeDeducer<Func, Ret(Func::*)(Args...) const> {
  using type = LightweightFunctionImpl<TransparentReferenceTransformer, Func, Ret, Args...>;
};

template<class Func, class Ret, class... Args>
struct TypeDeducer<Func, Ret(Func::*)(Args...)> {
  using type = LightweightFunctionImpl<MutableReferenceTransformer, Func, Ret, Args...>;
};

} // namespace lw_function_details

// Helper function to create an object that implements LightweightCallable interface from a lambda.
template<class Func>
auto make_lw_function(const Func& functor) {
  return typename lw_function_details::TypeDeducer<
      Func, decltype(&Func::operator())>::type(functor);
}

} // namespace yb
