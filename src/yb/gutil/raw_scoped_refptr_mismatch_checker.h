// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
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

#include "yb/gutil/ref_counted.h"
#include "yb/gutil/template_util.h"
#include "yb/gutil/tuple.h"

// It is dangerous to post a task with a T* argument where T is a subtype of
// RefCounted(Base|ThreadSafeBase), since by the time the parameter is used, the
// object may already have been deleted since it was not held with a
// scoped_refptr. Example: http://crbug.com/27191
// The following set of traits are designed to generate a compile error
// whenever this antipattern is attempted.

namespace yb {

// This is a base internal implementation file used by task.h and callback.h.
// Not for public consumption, so we wrap it in namespace internal.
namespace internal {

template <typename T>
struct NeedsScopedRefptrButGetsRawPtr {
#if defined(OS_WIN)
  enum {
    value = base::false_type::value
  };
#else
  enum {
    // Human readable translation: you needed to be a scoped_refptr if you are a
    // raw pointer type and are convertible to a RefCounted(Base|ThreadSafeBase)
    // type.
    value = (base::is_pointer<T>::value &&
             (base::is_convertible<T, subtle::RefCountedBase*>::value ||
              base::is_convertible<T, subtle::RefCountedThreadSafeBase*>::value))
  };
#endif
};

template <typename Params>
struct ParamsUseScopedRefptrCorrectly {
  enum { value = 0 };
};

template <>
struct ParamsUseScopedRefptrCorrectly<Tuple0> {
  enum { value = 1 };
};

template <typename A>
struct ParamsUseScopedRefptrCorrectly<Tuple1<A> > {
  enum { value = !NeedsScopedRefptrButGetsRawPtr<A>::value };
};

template <typename A, typename B>
struct ParamsUseScopedRefptrCorrectly<Tuple2<A, B> > {
  enum { value = !(NeedsScopedRefptrButGetsRawPtr<A>::value ||
                   NeedsScopedRefptrButGetsRawPtr<B>::value) };
};

template <typename A, typename B, typename C>
struct ParamsUseScopedRefptrCorrectly<Tuple3<A, B, C> > {
  enum { value = !(NeedsScopedRefptrButGetsRawPtr<A>::value ||
                   NeedsScopedRefptrButGetsRawPtr<B>::value ||
                   NeedsScopedRefptrButGetsRawPtr<C>::value) };
};

template <typename A, typename B, typename C, typename D>
struct ParamsUseScopedRefptrCorrectly<Tuple4<A, B, C, D> > {
  enum { value = !(NeedsScopedRefptrButGetsRawPtr<A>::value ||
                   NeedsScopedRefptrButGetsRawPtr<B>::value ||
                   NeedsScopedRefptrButGetsRawPtr<C>::value ||
                   NeedsScopedRefptrButGetsRawPtr<D>::value) };
};

template <typename A, typename B, typename C, typename D, typename E>
struct ParamsUseScopedRefptrCorrectly<Tuple5<A, B, C, D, E> > {
  enum { value = !(NeedsScopedRefptrButGetsRawPtr<A>::value ||
                   NeedsScopedRefptrButGetsRawPtr<B>::value ||
                   NeedsScopedRefptrButGetsRawPtr<C>::value ||
                   NeedsScopedRefptrButGetsRawPtr<D>::value ||
                   NeedsScopedRefptrButGetsRawPtr<E>::value) };
};

template <typename A, typename B, typename C, typename D, typename E,
          typename F>
struct ParamsUseScopedRefptrCorrectly<Tuple6<A, B, C, D, E, F> > {
  enum { value = !(NeedsScopedRefptrButGetsRawPtr<A>::value ||
                   NeedsScopedRefptrButGetsRawPtr<B>::value ||
                   NeedsScopedRefptrButGetsRawPtr<C>::value ||
                   NeedsScopedRefptrButGetsRawPtr<D>::value ||
                   NeedsScopedRefptrButGetsRawPtr<E>::value ||
                   NeedsScopedRefptrButGetsRawPtr<F>::value) };
};

template <typename A, typename B, typename C, typename D, typename E,
          typename F, typename G>
struct ParamsUseScopedRefptrCorrectly<Tuple7<A, B, C, D, E, F, G> > {
  enum { value = !(NeedsScopedRefptrButGetsRawPtr<A>::value ||
                   NeedsScopedRefptrButGetsRawPtr<B>::value ||
                   NeedsScopedRefptrButGetsRawPtr<C>::value ||
                   NeedsScopedRefptrButGetsRawPtr<D>::value ||
                   NeedsScopedRefptrButGetsRawPtr<E>::value ||
                   NeedsScopedRefptrButGetsRawPtr<F>::value ||
                   NeedsScopedRefptrButGetsRawPtr<G>::value) };
};

template <typename A, typename B, typename C, typename D, typename E,
          typename F, typename G, typename H>
struct ParamsUseScopedRefptrCorrectly<Tuple8<A, B, C, D, E, F, G, H> > {
  enum { value = !(NeedsScopedRefptrButGetsRawPtr<A>::value ||
                   NeedsScopedRefptrButGetsRawPtr<B>::value ||
                   NeedsScopedRefptrButGetsRawPtr<C>::value ||
                   NeedsScopedRefptrButGetsRawPtr<D>::value ||
                   NeedsScopedRefptrButGetsRawPtr<E>::value ||
                   NeedsScopedRefptrButGetsRawPtr<F>::value ||
                   NeedsScopedRefptrButGetsRawPtr<G>::value ||
                   NeedsScopedRefptrButGetsRawPtr<H>::value) };
};

}  // namespace internal

}  // namespace yb
