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

#include "yb/rocksdb/metadata.h"

#include "yb/rocksdb/db/filename.h"

#include "yb/util/format.h"

namespace rocksdb {

std::string FileBoundaryValuesBase::ToString() const {
  return yb::Format("{ seqno: $0 user_frontier: $1 }", seqno, user_frontier);
}

void UserFrontier::Update(const UserFrontier* rhs, UpdateUserValueType type, UserFrontierPtr* out) {
  if (!rhs) {
    return;
  }
  if (*out) {
    (**out).Update(*rhs, type);
  } else {
    *out = rhs->Clone();
  }
}

bool UserFrontier::Dominates(const UserFrontier& rhs, UpdateUserValueType update_type) const {
  // Check that this value, if updated with the given rhs, stays the same, and hence "dominates" it.
  std::unique_ptr<UserFrontier> copy = Clone();
  copy->Update(rhs, update_type);
  return Equals(*copy);
}

void UpdateUserFrontier(UserFrontierPtr* value, const UserFrontierPtr& update,
                        UpdateUserValueType type) {
  if (*value) {
    (**value).Update(*update, type);
  } else {
    *value = update;
  }
}

void UpdateUserFrontier(UserFrontierPtr* value, UserFrontierPtr&& update,
                        UpdateUserValueType type) {
  if (*value) {
    (**value).Update(*update, type);
  } else {
    *value = std::move(update);
  }
}

std::string UserFrontiers::ToString() const {
  return ::yb::Format("{ smallest: $0 largest: $1 }", Smallest(), Largest());
}

std::string SstFileMetaData::Name() const {
  return MakeTableFileName(/* path= */ "", name_id);
}

std::string SstFileMetaData::BaseFilePath() const {
  return MakeTableFileName(db_path, name_id);
}

std::string SstFileMetaData::DataFilePath() const {
  return MakeTableDataFilePath(db_path, name_id);
}

std::string LiveFileMetaData::ToString() const {
  return YB_STRUCT_TO_STRING(
      total_size, base_size, uncompressed_size, name_id, db_path, imported,
      being_compacted, column_family_name, level, smallest, largest);
}

void UpdateUserValue(
    UserBoundaryValues* values, UserBoundaryTag tag, const Slice& new_value,
    UpdateUserValueType type) {
  int compare_sign = static_cast<int>(type);
  for (auto& value : *values) {
    if (value.tag == tag) {
      if (value.CompareTo(new_value) * compare_sign > 0) {
        value.value.Assign(new_value);
      }
      return;
    }
  }
  values->emplace_back(tag, new_value);
}

void UpdateUserValue(
    UserBoundaryValues* values, const UserBoundaryValueRef& new_value, UpdateUserValueType type) {
  UpdateUserValue(values, new_value.tag, new_value.AsSlice(), type);
}

void UpdateUserValue(
    UserBoundaryValues* values, const UserBoundaryValue& new_value, UpdateUserValueType type) {
  UpdateUserValue(values, new_value.tag, new_value.AsSlice(), type);
}

void UpdateUserValues(
    const UserBoundaryValueRefs& source, UpdateUserValueType type, UserBoundaryValues* values) {
  for (const auto& user_value : source) {
    UpdateUserValue(values, user_value, type);
  }
}

void UpdateUserValues(
    const UserBoundaryValues& source, UpdateUserValueType type, UserBoundaryValues* values) {
  for (const auto& user_value : source) {
    UpdateUserValue(values, user_value, type);
  }
}

} // namespace rocksdb
