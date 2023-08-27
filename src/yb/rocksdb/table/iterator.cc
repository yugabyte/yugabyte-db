//  Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under the BSD-style license found in the
//  LICENSE file in the root directory of this source tree. An additional grant
//  of patent rights can be found in the PATENTS file in the same directory.
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
// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include "yb/rocksdb/iterator.h"
#include "yb/rocksdb/table/internal_iterator.h"
#include "yb/rocksdb/util/arena.h"

namespace rocksdb {

Cleanable::Cleanable() {
  cleanup_.function = nullptr;
  cleanup_.next = nullptr;
}

Cleanable::~Cleanable() {
  if (cleanup_.function != nullptr) {
    (*cleanup_.function)(cleanup_.arg1, cleanup_.arg2);
    for (Cleanup* c = cleanup_.next; c != nullptr; ) {
      (*c->function)(c->arg1, c->arg2);
      Cleanup* next = c->next;
      delete c;
      c = next;
    }
  }
}

void Cleanable::RegisterCleanup(CleanupFunction func, void* arg1, void* arg2) {
  assert(func != nullptr);
  Cleanup* c;
  if (cleanup_.function == nullptr) {
    c = &cleanup_;
  } else {
    c = new Cleanup;
    c->next = cleanup_.next;
    cleanup_.next = c;
  }
  c->function = func;
  c->arg1 = arg1;
  c->arg2 = arg2;
}

Status Iterator::GetProperty(std::string prop_name, std::string* prop) {
  if (prop == nullptr) {
    return STATUS(InvalidArgument, "prop is nullptr");
  }
  if (prop_name == "rocksdb.iterator.is-key-pinned") {
    *prop = "0";
    return Status::OK();
  }
  return STATUS(InvalidArgument, "Undentified property.");
}

namespace {

class EmptyIterator final : public Iterator {
 public:
  explicit EmptyIterator(const Status& s) : status_(s) { }
  const KeyValueEntry& Entry() const override {
    return KeyValueEntry::Invalid();
  }

  const KeyValueEntry& Seek(Slice target) override {
    return Entry();
  }

  const KeyValueEntry& SeekToFirst() override {
    return Entry();
  }

  const KeyValueEntry& SeekToLast() override {
    return Entry();
  }

  const KeyValueEntry& Next() override {
    assert(false);
    return Entry();
  }

  const KeyValueEntry& Prev() override {
    assert(false);
    return Entry();
  }

  Status status() const override { return status_; }

 private:
  Status status_;
};

class EmptyInternalIterator : public InternalIterator {
 public:
  explicit EmptyInternalIterator(const Status& s) : status_(s) {}

  const KeyValueEntry& Entry() const override { return KeyValueEntry::Invalid(); }

  const KeyValueEntry& Seek(Slice target) override {
    return Entry();
  }

  const KeyValueEntry& SeekToFirst() override {
    return Entry();
  }

  const KeyValueEntry& SeekToLast() override {
    return Entry();
  }

  const KeyValueEntry& Next() override {
    assert(false);
    return Entry();
  }

  const KeyValueEntry& Prev() override {
    assert(false);
    return Entry();
  }

  Status status() const override { return status_; }

 private:
  Status status_;
};
}  // namespace

Iterator* NewEmptyIterator() {
  return new EmptyIterator(Status::OK());
}

Iterator* NewErrorIterator(const Status& status) {
  return new EmptyIterator(status);
}

InternalIterator* NewEmptyInternalIterator() {
  return new EmptyInternalIterator(Status::OK());
}

InternalIterator* NewEmptyInternalIterator(Arena* arena) {
  if (arena == nullptr) {
    return NewEmptyInternalIterator();
  } else {
    auto mem = arena->AllocateAligned(sizeof(EmptyIterator));
    return new (mem) EmptyInternalIterator(Status::OK());
  }
}

InternalIterator* NewErrorInternalIterator(const Status& status) {
  return new EmptyInternalIterator(status);
}

InternalIterator* NewErrorInternalIterator(const Status& status, Arena* arena) {
  if (arena == nullptr) {
    return NewErrorInternalIterator(status);
  } else {
    auto mem = arena->AllocateAligned(sizeof(EmptyIterator));
    return new (mem) EmptyInternalIterator(status);
  }
}

}  // namespace rocksdb
