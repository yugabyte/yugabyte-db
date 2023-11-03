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

#include "yb/util/memory/tracked_shared_ptr.h"

#include <map>

#include <boost/functional/hash.hpp>

namespace { // NOLINT

struct StackWithIsNotNull {
  yb::StackTrace* stack_trace;
  bool is_not_nullptr;

  size_t HashCode() const {
    size_t hash = 0;
    boost::hash_combine(hash, stack_trace);
    boost::hash_combine(hash, is_not_nullptr);
    return hash;
  }

  int compare(const StackWithIsNotNull& other) const {
    int cmp = is_not_nullptr - other.is_not_nullptr;
    return cmp ? cmp : stack_trace->compare(*other.stack_trace);
  }
};

__attribute__((unused)) bool operator==(
    const StackWithIsNotNull& lhs, const StackWithIsNotNull& rhs) {
  return lhs.is_not_nullptr == rhs.is_not_nullptr && lhs.stack_trace == rhs.stack_trace;
}

__attribute__((unused)) bool operator<(
    const StackWithIsNotNull& lhs, const StackWithIsNotNull& rhs) {
  return lhs.compare(rhs) < 0;
}

template <class Key, class Value>
void IncrementMapEntry(std::map<Key, Value>* map, const Key& key) {
  if (map->count(key) > 0) {
    (*map)[key]++;
  } else {
    (*map)[key] = 1;
  }
}

} // namespace

namespace std {

template <>
struct hash<StackWithIsNotNull> {
  std::size_t operator()(StackWithIsNotNull const& obj) const noexcept { return obj.HashCode(); }
};

} // namespace std

namespace yb {

template <class T>
std::atomic<int64_t> TrackedSharedPtr<T>::num_references_{0};

template <class T>
std::atomic<int64_t> TrackedSharedPtr<T>::num_instances_{0};

template <class T>
simple_spinlock TrackedSharedPtr<T>::lock_;

template <class T>
std::set<TrackedSharedPtr<T>*> TrackedSharedPtr<T>::instances_;

template <class T>
void TrackedSharedPtr<T>::Dump() {
  std::lock_guard l(lock_);
  LOG(INFO) << "num_references: " << num_references_;
  LOG(INFO) << "num_instances: " << num_instances_;

  std::map<StackWithIsNotNull, size_t> count_by_stack_trace_created;

  size_t count_non_nullptr = 0;
  LOG(INFO) << "instances: ";
  for (auto& instance : instances_) {
    LOG(INFO) << instance << " ptr:" << instance->get()
              << " last_not_null_value: " << AsString(instance->last_not_null_value_)
              << " use_count: " << instance->use_count();

    const bool is_not_nullptr = instance->get();
    if (is_not_nullptr) {
      ++count_non_nullptr;
    }
    IncrementMapEntry(&count_by_stack_trace_created,
        StackWithIsNotNull{&instance->stack_trace_created_, is_not_nullptr});
  }

  LOG(INFO) << "count_non_nullptr: " << count_non_nullptr;
  LOG(INFO) << "Tracked shared pointers alive by stack trace created:";
  for (auto& entry : count_by_stack_trace_created) {
    LOG(INFO) << "not_null: " << entry.first.is_not_nullptr << " count: " << entry.second
              << " stack_trace: " << entry.first.stack_trace->Symbolize();
  }
  LOG(INFO) << "<<<";
  CHECK_EQ(num_instances_, instances_.size());
}

template <class T>
void TrackedSharedPtr<T>::RefIfInitialized() {
  if (IsInitialized()) {
    last_not_null_value_ = std::shared_ptr<T>::get();
    ++num_references_;
  }
}

template <class T>
void TrackedSharedPtr<T>::UnrefIfInitialized() {
  if (IsInitialized()) {
    --num_references_;
  }
}

template <class T>
void TrackedSharedPtr<T>::RegisterInstance() {
  std::lock_guard l(lock_);
  ++num_instances_;
  instances_.insert(this);
  // We skip 3 frames: StackTrace::Collect, TrackedSharedPtr::RegisterInstance and
  // TrackedSharedPtr constructor to only have stack trace for code we want to debug.
  stack_trace_created_.Collect(/* skip_frames =*/ 3);
}

template <class T>
void TrackedSharedPtr<T>::UnregisterInstance() {
  std::lock_guard l(lock_);
  --num_instances_;
  instances_.erase(this);
}

}  // namespace yb
