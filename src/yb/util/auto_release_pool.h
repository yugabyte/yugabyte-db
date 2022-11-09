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
// Simple pool of objects that will be deallocated when the pool is
// destroyed

#pragma once

#include <vector>

#include "yb/gutil/spinlock.h"

namespace yb {

// Thread-safe.
class AutoReleasePool {
 public:
  AutoReleasePool(): objects_() { }

  ~AutoReleasePool() {
    for (auto& object : objects_) {
      delete object;
    }
  }

  template <class T>
  T *Add(T *t) {
    base::SpinLockHolder l(&lock_);
    objects_.push_back(new SpecificElement<T>(t));
    return t;
  }

  // Add an array-allocated object to the pool. This is identical to
  // Add() except that it will be freed with 'delete[]' instead of 'delete'.
  template<class T>
  T* AddArray(T *t) {
    base::SpinLockHolder l(&lock_);
    objects_.push_back(new SpecificArrayElement<T>(t));
    return t;
  }

  // Donate all objects in this pool to another pool.
  void DonateAllTo(AutoReleasePool* dst) {
    base::SpinLockHolder l(&lock_);
    base::SpinLockHolder l_them(&dst->lock_);

    dst->objects_.reserve(dst->objects_.size() + objects_.size());
    dst->objects_.insert(dst->objects_.end(), objects_.begin(), objects_.end());
    objects_.clear();
  }

 private:
  struct GenericElement {
    virtual ~GenericElement() {}
  };

  template <class T>
  struct SpecificElement : GenericElement {
    explicit SpecificElement(T *t_): t(t_) {}
    ~SpecificElement() {
      delete t;
    }

    T *t;
  };

  template <class T>
  struct SpecificArrayElement : GenericElement {
    explicit SpecificArrayElement(T *t_): t(t_) {}
    ~SpecificArrayElement() {
      delete [] t;
    }

    T *t;
  };

  typedef std::vector<GenericElement *> ElementVector;
  ElementVector objects_;
  base::SpinLock lock_;
};


} // namespace yb
