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

#include "yb/rocksdb/write_batch_base.h"

#include <string>

#include "yb/util/slice.h"

namespace rocksdb {

// Simple implementation of SlicePart variants of Put().  Child classes
// can override these method with more performant solutions if they choose.
void WriteBatchBase::Put(ColumnFamilyHandle* column_family,
                         const yb::SliceParts& key, const SliceParts& value) {
  std::string key_buf, value_buf;
  Slice key_slice(key, &key_buf);
  Slice value_slice(value, &value_buf);

  Put(column_family, key_slice, value_slice);
}

void WriteBatchBase::Put(const SliceParts& key, const SliceParts& value) {
  std::string key_buf, value_buf;
  Slice key_slice(key, &key_buf);
  Slice value_slice(value, &value_buf);

  Put(key_slice, value_slice);
}

void WriteBatchBase::Delete(ColumnFamilyHandle* column_family,
                            const SliceParts& key) {
  std::string key_buf;
  Slice key_slice(key, &key_buf);
  Delete(column_family, key_slice);
}

void WriteBatchBase::Delete(const SliceParts& key) {
  std::string key_buf;
  Slice key_slice(key, &key_buf);
  Delete(key_slice);
}

void WriteBatchBase::SingleDelete(ColumnFamilyHandle* column_family,
                                  const SliceParts& key) {
  std::string key_buf;
  Slice key_slice(key, &key_buf);
  SingleDelete(column_family, key_slice);
}

void WriteBatchBase::SingleDelete(const SliceParts& key) {
  std::string key_buf;
  Slice key_slice(key, &key_buf);
  SingleDelete(key_slice);
}

void WriteBatchBase::Merge(ColumnFamilyHandle* column_family,
                         const SliceParts& key, const SliceParts& value) {
  std::string key_buf, value_buf;
  Slice key_slice(key, &key_buf);
  Slice value_slice(value, &value_buf);

  Merge(column_family, key_slice, value_slice);
}

void WriteBatchBase::Merge(const SliceParts& key, const SliceParts& value) {
  std::string key_buf, value_buf;
  Slice key_slice(key, &key_buf);
  Slice value_slice(value, &value_buf);

  Merge(key_slice, value_slice);
}

}  // namespace rocksdb
