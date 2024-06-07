// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.
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

#include <stddef.h>

#include "yb/gutil/once.h"
#include "yb/util/atomic.h"
#include "yb/util/status.h"

namespace yb {

class YBOnceDynamic;

namespace internal {

// Cheap, single-arg "bound callback" (similar to yb::Callback) for use
// in YBOnceDynamic.
template<typename T>
struct MemberFunc {
  YBOnceDynamic* once;
  T* instance;
  Status (T::*member_func)();
};

template<typename T>
void InitCb(void* arg) {
  MemberFunc<T>* mf = reinterpret_cast<MemberFunc<T>*>(arg);
  mf->once->status_ = (mf->instance->*mf->member_func)();
  mf->once->set_initted();
}

} // namespace internal

// More versatile version of GoogleOnceDynamic, including the following:
// 1. Can be used with single-arg, non-static member functions.
// 2. Retains results and overall initialization state for repeated access.
// 3. Access to initialization state is safe for concurrent use.
class YBOnceDynamic {
 public:
  YBOnceDynamic()
    : initted_(false) {
  }

  // If the underlying GoogleOnceDynamic has yet to be invoked, invokes the
  // provided member function and stores its return value. Otherwise,
  // returns the stored Status.
  //
  // T: the type of the member passed in.
  template<typename T>
  Status Init(Status (T::*member_func)(), T* instance) {
    internal::MemberFunc<T> mf = { this, instance, member_func };

    // Clang UBSAN doesn't like it when GoogleOnceDynamic handles the cast
    // of the argument:
    //
    //   runtime error: call to function
    //   yb::cfile::BloomFileReader::InitOnceCb(yb::cfile::BloomFileReader*)
    //   through pointer to incorrect function type 'void (*)(void *)'
    //
    // So let's do the cast ourselves, to void* here and back in InitCb().
    once_.Init(&internal::InitCb<T>, reinterpret_cast<void*>(&mf));
    return status_;
  }

  // kMemOrderAcquire ensures that loads/stores that come after initted()
  // aren't reordered to come before it instead. kMemOrderRelease ensures
  // the opposite (i.e. loads/stores before set_initted() aren't reordered
  // to come after it).
  //
  // Taken together, threads can safely synchronize on initted_.
  bool initted() const { return initted_.Load(kMemOrderAcquire); }

  // Returns the memory usage of this object without the object itself. Should
  // be used when embedded inside another object.
  size_t memory_footprint_excluding_this() const;

  // Returns the memory usage of this object including the object itself.
  // Should be used when allocated on the heap.
  size_t memory_footprint_including_this() const;

 private:
  template<typename T>
  friend void internal::InitCb(void* arg);

  void set_initted() { initted_.Store(true, kMemOrderRelease); }

  AtomicBool initted_;
  GoogleOnceDynamic once_;
  Status status_;
};

} // namespace yb
