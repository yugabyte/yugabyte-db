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

#include "yb/docdb/consensus_frontier.h"

#include <google/protobuf/any.pb.h>

#include "yb/docdb/docdb.pb.h"
#include "yb/gutil/casts.h"
#include "yb/util/tostring.h"

namespace yb {
namespace docdb {

ConsensusFrontier::~ConsensusFrontier() {
}

bool ConsensusFrontier::Equals(const UserFrontier& pre_rhs) const {
  const ConsensusFrontier& rhs = down_cast<const ConsensusFrontier&>(pre_rhs);
  return op_id_ == rhs.op_id_ &&
         hybrid_time_ == rhs.hybrid_time_ &&
         history_cutoff_ == rhs.history_cutoff_ &&
         hybrid_time_filter_ == rhs.hybrid_time_filter_ &&
         max_value_level_ttl_expiration_time_ == rhs.max_value_level_ttl_expiration_time_;
}

void ConsensusFrontier::ToPB(google::protobuf::Any* any) const {
  ConsensusFrontierPB pb;
  op_id_.ToPB(pb.mutable_op_id());
  pb.set_hybrid_time(hybrid_time_.ToUint64());
  pb.set_history_cutoff(history_cutoff_.ToUint64());
  if (hybrid_time_filter_.is_valid()) {
    pb.set_hybrid_time_filter(hybrid_time_filter_.ToUint64());
  }
  pb.set_max_value_level_ttl_expiration_time(max_value_level_ttl_expiration_time_.ToUint64());
  any->PackFrom(pb);
}

void ConsensusFrontier::FromPB(const google::protobuf::Any& any) {
  ConsensusFrontierPB pb;
  any.UnpackTo(&pb);
  op_id_ = OpId::FromPB(pb.op_id());
  hybrid_time_ = HybridTime(pb.hybrid_time());
  history_cutoff_ = NormalizeHistoryCutoff(HybridTime(pb.history_cutoff()));
  if (pb.has_hybrid_time_filter()) {
    hybrid_time_filter_ = HybridTime(pb.hybrid_time_filter());
  } else {
    hybrid_time_filter_ = HybridTime();
  }
  max_value_level_ttl_expiration_time_ =
      HybridTime::FromPB(pb.max_value_level_ttl_expiration_time());
}

void ConsensusFrontier::FromOpIdPBDeprecated(const OpIdPB& pb) {
  op_id_ = OpId::FromPB(pb);
}

std::string ConsensusFrontier::ToString() const {
  return YB_CLASS_TO_STRING(
      op_id, hybrid_time, history_cutoff, hybrid_time_filter, max_value_level_ttl_expiration_time);
}

namespace {

// Check if the given updated value is a correct "update" for the given previous value in the
// specified direction. If one of the two values is not defined according to the bool conversion,
// then there is no error.
template<typename T>
bool IsUpdateValidForField(
    const T& this_value, const T& updated_value, rocksdb::UpdateUserValueType update_type) {
  if (!this_value || !updated_value) {
    // If any of the two values is undefined, we don't treat this as an error.
    return true;
  }
  switch (update_type) {
    case rocksdb::UpdateUserValueType::kLargest:
      return updated_value >= this_value;
    case rocksdb::UpdateUserValueType::kSmallest:
      return updated_value <= this_value;
  }
  FATAL_INVALID_ENUM_VALUE(rocksdb::UpdateUserValueType, update_type);
}

template<typename T>
void UpdateField(
    T* this_value, const T& new_value, rocksdb::UpdateUserValueType update_type) {
  switch (update_type) {
    case rocksdb::UpdateUserValueType::kLargest:
      this_value->MakeAtLeast(new_value);
      return;
    case rocksdb::UpdateUserValueType::kSmallest:
      this_value->MakeAtMost(new_value);
      return;
  }
  FATAL_INVALID_ENUM_VALUE(rocksdb::UpdateUserValueType, update_type);
}

} // anonymous namespace

void ConsensusFrontier::Update(
    const rocksdb::UserFrontier& pre_rhs, rocksdb::UpdateUserValueType update_type) {
  const ConsensusFrontier& rhs = down_cast<const ConsensusFrontier&>(pre_rhs);
  UpdateField(&op_id_, rhs.op_id_, update_type);
  UpdateField(&hybrid_time_, rhs.hybrid_time_, update_type);
  UpdateField(&history_cutoff_, rhs.history_cutoff_, update_type);
  // Reset filter after compaction.
  hybrid_time_filter_ = HybridTime();
  UpdateField(&max_value_level_ttl_expiration_time_,
      rhs.max_value_level_ttl_expiration_time_, update_type);
}

Slice ConsensusFrontier::Filter() const {
  return hybrid_time_filter_.is_valid()
      ? Slice(pointer_cast<const char*>(&hybrid_time_filter_), sizeof(hybrid_time_filter_))
      : Slice();
}

bool ConsensusFrontier::IsUpdateValid(
    const rocksdb::UserFrontier& pre_rhs, rocksdb::UpdateUserValueType update_type) const {
  const ConsensusFrontier& rhs = down_cast<const ConsensusFrontier&>(pre_rhs);

  // We don't check history cutoff here, because it is not an error when the the history cutoff
  // for a later compaction is lower than that for an earlier compaction. This can happen if
  // FLAGS_timestamp_history_retention_interval_sec increases.
  return IsUpdateValidForField(op_id_, rhs.op_id_, update_type) &&
         IsUpdateValidForField(hybrid_time_, rhs.hybrid_time_, update_type);
}

} // namespace docdb
} // namespace yb
