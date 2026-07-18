// Copyright (c) YugabyteDB, Inc.
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

#include <bitset>

namespace yb::docdb {

class StorageSet {
 public:
  StorageSet() = default;

  static StorageSet All() {
    StorageSet result;
    result.bits_.set();
    return result;
  }

  bool Any() const {
    return bits_.any();
  }

  bool TestRegularDB() const {
    return bits_.test(0);
  }

  void SetRegularDB(bool value = true) {
    bits_.set(0, value);
  }

  void ResetRegularDB() {
    bits_.reset(0);
  }

  bool TestVectorIndex(size_t index) const {
    return bits_.test(1 + index);
  }

  void SetVectorIndex(size_t index, bool value = true) {
    bits_.set(1 + index, value);
  }

  void ResetVectorIndex(size_t index) {
    bits_.reset(1 + index);
  }

  std::string ToString() const {
    return AsString(bits_);
  }

 private:
  // Entry with index 0 - regular db.
  // Entry with index i (>0) - vector_index[i - 1]
  // TODO(vector_index) Block creating too many indexes.
  std::bitset<64> bits_;
};

}  // namespace yb::docdb
