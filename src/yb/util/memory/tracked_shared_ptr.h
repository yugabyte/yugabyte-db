// Copyright (c) YugaByte, Inc.
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

#include <stdint.h>

#include <atomic>
#include <functional>
#include <memory>
#include <type_traits>

#include "yb/util/flags.h"

#include "yb/gutil/integral_types.h"

#include "yb/util/debug-util.h"
#include "yb/util/locks.h"
#include "yb/util/math_util.h"
#include "yb/util/shared_lock.h"
#include "yb/util/thread.h"

namespace yb {

// Usage:
// - Replace std::shared_ptr<SomeClass> with TrackedSharedPtr<SomeClass>.
// - Call TrackedSharedPtr<SomeClass>::Dump() to log which shared pointers currently holding
// references to SomeClass instances and stack traces where these pointers were created.
template <class T>
class TrackedSharedPtr : public std::shared_ptr<T> {
 public:
  TrackedSharedPtr() {
    RegisterInstance();
  }

  explicit TrackedSharedPtr(T* ptr) : std::shared_ptr<T>(ptr) {
    RefIfInitialized();
    RegisterInstance();
  }

  // We don't use explicit here to allow storing std::make_shared<T> result into
  // TrackedSharedPtr<T>.
  TrackedSharedPtr(const std::shared_ptr<T>& other) : std::shared_ptr<T>(other) { // NOLINT
    RefIfInitialized();
    RegisterInstance();
  }

  TrackedSharedPtr(const TrackedSharedPtr& other) : std::shared_ptr<T>(other) {
    RefIfInitialized();
    RegisterInstance();
  }

  TrackedSharedPtr(TrackedSharedPtr&& other) {
    operator=(other);
    RegisterInstance();
  }

  bool IsInitialized() const { return std::shared_ptr<T>::get(); }

  TrackedSharedPtr& operator=(const TrackedSharedPtr& other) {
    UnrefIfInitialized();
    std::shared_ptr<T>::operator=(other);
    RefIfInitialized();
    return *this;
  }

  TrackedSharedPtr& operator=(TrackedSharedPtr&& other) {
    UnrefIfInitialized();
    other.UnrefIfInitialized();
    std::shared_ptr<T>::operator=(std::move(other));
    RefIfInitialized();
    other.RefIfInitialized();
    return *this;
  }

  void reset(T* ptr = nullptr) {
    UnrefIfInitialized();
    std::shared_ptr<T>::reset(ptr);
    RefIfInitialized();
  }

  virtual ~TrackedSharedPtr() {
    UnrefIfInitialized();
    UnregisterInstance();
  }

  static void Dump();

  static int64_t num_references()  { return num_references_; }
  static int64_t num_instances() { return num_instances_; }

 private:
  void RegisterInstance();

  void UnregisterInstance();

  void RefIfInitialized();

  void UnrefIfInitialized();

  T* last_not_null_value_ = nullptr;
  StackTrace stack_trace_created_;

  static std::atomic<int64_t> num_references_;
  static std::atomic<int64_t> num_instances_;
  static simple_spinlock lock_;
  static std::set<TrackedSharedPtr<T>*> instances_;
};

}  // namespace yb
