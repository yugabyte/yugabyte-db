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

#include <unistd.h>

#include <list>
#include <string>
#include <type_traits>
#include <vector>

#include "yb/util/condition_variable.h"
#include "yb/util/mutex.h"

namespace yb {

// Return values for BlockingQueue::Put()
enum QueueStatus {
  QUEUE_SUCCESS = 0,
  QUEUE_SHUTDOWN = 1,
  QUEUE_FULL = 2
};

// Default logical length implementation: always returns 1.
struct DefaultLogicalSize {
  template<typename T>
  static size_t logical_size(const T& /* unused */) {
    return 1;
  }
};

template <typename T, class LOGICAL_SIZE = DefaultLogicalSize>
class BlockingQueue {
 public:
  // If T is a pointer, this will be the base type.  If T is not a pointer, you
  // can ignore this and the functions which make use of it.
  // Template substitution failure is not an error.
  typedef typename std::remove_pointer<T>::type T_VAL;

  explicit BlockingQueue(size_t max_size)
    : shutdown_(false),
      size_(0),
      max_size_(max_size),
      not_empty_(&lock_),
      not_full_(&lock_) {
  }

  // If the queue holds a bare pointer, it must be empty on destruction, since
  // it may have ownership of the pointer.
  ~BlockingQueue() {
    DCHECK(list_.empty() || !std::is_pointer<T>::value)
        << "BlockingQueue holds bare pointers at destruction time";
  }

  // Get an element from the queue. Returns false if we were shut down prior to
  // getting the element.
  bool BlockingGet(T *out) {
    MutexLock l(lock_);
    while (true) {
      if (!list_.empty()) {
        *out = list_.front();
        list_.pop_front();
        decrement_size_unlocked(*out);
        not_full_.Signal();
        return true;
      }
      if (shutdown_) {
        return false;
      }
      not_empty_.Wait();
    }
  }

  // Get an element from the queue. Returns false if the queue is empty and
  // we were shut down prior to getting the element.
  bool BlockingGet(std::unique_ptr<T_VAL> *out) {
    T t = NULL;
    bool got_element = BlockingGet(&t);
    if (!got_element) {
      return false;
    }
    out->reset(t);
    return true;
  }

  // Get all elements from the queue and append them to a
  // vector. Returns false if shutdown prior to getting the elements.
  bool BlockingDrainTo(std::vector<T>* out) {
    return BlockingDrainTo(out, MonoTime::kMax);
  }

  bool BlockingDrainTo(std::vector<T>* out, const MonoTime& wait_timeout_deadline) {
    MutexLock l(lock_);
    while (true) {
      if (!list_.empty()) {
        out->reserve(list_.size());
        for (const T& elt : list_) {
          out->push_back(elt);
          decrement_size_unlocked(elt);
        }
        list_.clear();
        not_full_.Signal();
        return true;
      }
      if (shutdown_) {
        return false;
      }
      if (!not_empty_.WaitUntil(wait_timeout_deadline)) {
        return true;
      }
    }
  }

  // Attempts to put the given value in the queue.
  // Returns:
  //   QUEUE_SUCCESS: if successfully inserted
  //   QUEUE_FULL: if the queue has reached max_size
  //   QUEUE_SHUTDOWN: if someone has already called Shutdown()
  QueueStatus Put(const T &val) {
    MutexLock l(lock_);
    if (size_ >= max_size_) {
      return QUEUE_FULL;
    }
    if (shutdown_) {
      return QUEUE_SHUTDOWN;
    }
    list_.push_back(val);
    increment_size_unlocked(val);
    l.Unlock();
    not_empty_.Signal();
    return QUEUE_SUCCESS;
  }

  // Returns the same as the other Put() overload above.
  // If the element was inserted, the std::unique_ptr releases its contents.
  QueueStatus Put(std::unique_ptr<T_VAL> *val) {
    QueueStatus s = Put(val->get());
    if (s == QUEUE_SUCCESS) {
      val->release();
    }
    return s;
  }

  // Gets an element for the queue; if the queue is full, blocks until
  // space becomes available. Returns false if we were shutdown prior
  // to enqueuing the element.
  bool BlockingPut(const T& val) {
    MutexLock l(lock_);
    while (true) {
      if (shutdown_) {
        return false;
      }
      if (size_ < max_size_) {
        list_.push_back(val);
        increment_size_unlocked(val);
        l.Unlock();
        not_empty_.Signal();
        return true;
      }
      not_full_.Wait();
    }
  }

  // Same as other BlockingPut() overload above. If the element was
  // enqueued, std::unique_ptr releases its contents.
  bool BlockingPut(std::unique_ptr<T_VAL>* val) {
    bool ret = Put(val->get());
    if (ret) {
      val->release();
    }
    return ret;
  }

  // Shut down the queue.
  // When a blocking queue is shut down, no more elements can be added to it,
  // and Put() will return QUEUE_SHUTDOWN.
  // Existing elements will drain out of it, and then BlockingGet will start
  // returning false.
  void Shutdown() {
    MutexLock l(lock_);
    shutdown_ = true;
    not_full_.Broadcast();
    not_empty_.Broadcast();
  }

  bool empty() const {
    MutexLock l(lock_);
    return list_.empty();
  }

  size_t max_size() const {
    return max_size_;
  }

  std::string ToString() const {
    std::string ret;

    MutexLock l(lock_);
    for (const T& t : list_) {
      ret.append(t->ToString());
      ret.append("\n");
    }
    return ret;
  }

 private:

  // Increments queue size. Must be called when 'lock_' is held.
  void increment_size_unlocked(const T& t) {
    size_ += LOGICAL_SIZE::logical_size(t);
  }

  // Decrements queue size. Must be called when 'lock_' is held.
  void decrement_size_unlocked(const T& t) {
    size_ -= LOGICAL_SIZE::logical_size(t);
  }

  bool shutdown_;
  size_t size_;
  size_t max_size_;
  mutable Mutex lock_;
  ConditionVariable not_empty_;
  ConditionVariable not_full_;
  std::list<T> list_;
};

} // namespace yb
