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
// The following only applies to changes made to this file as part of YugabyteDB development.
//
// Portions Copyright (c) YugabyteDB, Inc.
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

#include <memory>
#include <utility>

namespace yb {

// An abstract base class defining the interface for a polymorphic iterator.
template <typename ValueType>
class AbstractIterator {
 public:
  virtual ~AbstractIterator() = default;
  virtual void Next() = 0;
  virtual ValueType Dereference() const = 0;
  virtual bool NotEquals(const AbstractIterator& other) const = 0;
};

// PolymorphicIterator is a type-erased iterator wrapper around AbstractIterator<ValueType>.
// It allows iteration over any AbstractIterator implementation using standard iterator syntax.
// Construct with a unique_ptr to an AbstractIterator-derived instance.
template <typename ValueType>
class PolymorphicIterator {
 public:
  explicit PolymorphicIterator(std::unique_ptr<AbstractIterator<ValueType>> impl)
      : impl_(std::move(impl)) {}

  bool Valid() const {
    return impl_ != nullptr;
  }

  PolymorphicIterator& operator++() {
    impl_->Next();
    return *this;
  }

  ValueType operator*() const { return impl_->Dereference(); }

  bool operator!=(const PolymorphicIterator& other) const {
    return impl_ && other.impl_ && impl_->NotEquals(*other.impl_);
  }

 private:
  std::unique_ptr<AbstractIterator<ValueType>> impl_;
};

}  // namespace yb
