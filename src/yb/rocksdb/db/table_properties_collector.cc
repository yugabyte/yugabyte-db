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

#include "yb/rocksdb/db/table_properties_collector.h"

#include "yb/rocksdb/db/dbformat.h"
#include "yb/rocksdb/util/coding.h"
#include "yb/util/string_util.h"

namespace rocksdb {

Status InternalKeyPropertiesCollector::InternalAdd(const Slice& key,
                                                   const Slice& value,
                                                   uint64_t file_size) {
  ParsedInternalKey ikey;
  if (!ParseInternalKey(key, &ikey)) {
    return STATUS(InvalidArgument, "Invalid internal key");
  }

  // Note: We count both, deletions and single deletions here.
  if (ikey.type == ValueType::kTypeDeletion ||
      ikey.type == ValueType::kTypeSingleDeletion) {
    ++deleted_keys_;
  }

  return Status::OK();
}

Status InternalKeyPropertiesCollector::Finish(
    UserCollectedProperties* properties) {
  assert(properties);
  assert(properties->find(
        InternalKeyTablePropertiesNames::kDeletedKeys) == properties->end());
  std::string val;

  PutVarint64(&val, deleted_keys_);
  properties->insert({ InternalKeyTablePropertiesNames::kDeletedKeys, val });

  return Status::OK();
}

UserCollectedProperties
InternalKeyPropertiesCollector::GetReadableProperties() const {
  return {
    { "kDeletedKeys", ToString(deleted_keys_) }
  };
}

namespace {

EntryType GetEntryType(ValueType value_type) {
  switch (value_type) {
    case kTypeValue:
      return kEntryPut;
    case kTypeDeletion:
      return kEntryDelete;
    case kTypeSingleDeletion:
      return kEntrySingleDelete;
    case kTypeMerge:
      return kEntryMerge;
    default:
      return kEntryOther;
  }
}

}  // namespace

Status UserKeyTablePropertiesCollector::InternalAdd(const Slice& key,
                                                    const Slice& value,
                                                    uint64_t file_size) {
  ParsedInternalKey ikey;
  if (!ParseInternalKey(key, &ikey)) {
    return STATUS(InvalidArgument, "Invalid internal key");
  }

  return collector_->AddUserKey(ikey.user_key, value, GetEntryType(ikey.type),
                                ikey.sequence, file_size);
}

Status UserKeyTablePropertiesCollector::Finish(
    UserCollectedProperties* properties) {
  return collector_->Finish(properties);
}

UserCollectedProperties
UserKeyTablePropertiesCollector::GetReadableProperties() const {
  return collector_->GetReadableProperties();
}


const std::string InternalKeyTablePropertiesNames::kDeletedKeys
  = "rocksdb.deleted.keys";

uint64_t GetDeletedKeys(
    const UserCollectedProperties& props) {
  auto pos = props.find(InternalKeyTablePropertiesNames::kDeletedKeys);
  if (pos == props.end()) {
    return 0;
  }
  Slice raw = pos->second;
  uint64_t val = 0;
  return GetVarint64(&raw, &val) ? val : 0;
}

}  // namespace rocksdb
