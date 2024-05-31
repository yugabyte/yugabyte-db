//
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
//

#pragma once

#include <stddef.h>

#include <atomic>

namespace yb {

template <class Value>
class VersionTrackerCheckOut;

// Utility class to track version of stored data.
// VersionTracker<T> provides read access to data.
// If data should be modified, then it should be checked out:
// auto checkout = versioned_data.CheckOut();
// And checkout would provide write access to data.
// After checkout is destroyed, version is incremented.
template <class Value>
class VersionTracker {
 public:
  VersionTracker(const VersionTracker&) = delete;
  void operator=(const VersionTracker&) = delete;

  VersionTracker() = default;

  // Data is modified in place, so external synchronization is required to prevent
  // undesired access to partially modified data.
  VersionTrackerCheckOut<Value> CheckOut();

  void Commit() {
    version_.fetch_add(1, std::memory_order_acq_rel);
  }

  const Value& operator*() const {
    return value_;
  }

  const Value* operator->() const {
    return &value_;
  }

  size_t Version() const {
    return version_.load(std::memory_order_acquire);
  }

 private:
  friend class VersionTrackerCheckOut<Value>;

  Value value_;
  std::atomic<size_t> version_{0};
};

template <class Value>
class VersionTrackerCheckOut {
 public:
  VersionTrackerCheckOut(const VersionTrackerCheckOut&) = delete;
  void operator=(const VersionTrackerCheckOut&) = delete;

  VersionTrackerCheckOut(VersionTrackerCheckOut&& rhs) : tracker_(rhs.tracker_) {
    rhs.tracker_ = nullptr;
  }

  explicit VersionTrackerCheckOut(VersionTracker<Value>* tracker) : tracker_(tracker) {}

  ~VersionTrackerCheckOut() {
    if (tracker_) {
      tracker_->Commit();
    }
  }

  Value& operator*() {
    return tracker_->value_;
  }

  Value* operator->() {
    return get_ptr();
  }

  Value* get_ptr() {
    return &tracker_->value_;
  }

 private:
  VersionTracker<Value>* tracker_;
};

template <class Value>
VersionTrackerCheckOut<Value> VersionTracker<Value>::CheckOut() {
  return VersionTrackerCheckOut<Value>(this);
}

} // namespace yb
