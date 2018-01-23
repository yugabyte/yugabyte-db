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

#include "yb/docdb/docdb.pb.h"

namespace yb {
namespace docdb {

bool ConsensusFrontier::Equals(const UserFrontier& pre_rhs) const {
  const ConsensusFrontier& rhs = down_cast<const ConsensusFrontier&>(pre_rhs);
  return op_id_ == rhs.op_id_ && ht_ == rhs.ht_;
}

void ConsensusFrontier::ToPB(google::protobuf::Any* any) const {
  ConsensusFrontierPB pb;
  op_id_.ToPB(pb.mutable_op_id());
  pb.set_hybrid_time(ht_.ToUint64());
  any->PackFrom(pb);
}

void ConsensusFrontier::FromPB(const google::protobuf::Any& any) {
  ConsensusFrontierPB pb;
  any.UnpackTo(&pb);
  op_id_ = OpId::FromPB(pb.op_id());
  ht_ = HybridTime(pb.hybrid_time());
}

void ConsensusFrontier::FromOpIdPBDeprecated(const OpIdPB& pb) {
  op_id_ = OpId::FromPB(pb);
}

std::string ConsensusFrontier::ToString() const {
  return yb::Format("{ op_id: $0 hybrid_time: $1 }", op_id_, ht_);
}

void ConsensusFrontier::Update(
    const rocksdb::UserFrontier& pre_rhs, rocksdb::UpdateUserValueType type) {
  const ConsensusFrontier& rhs = down_cast<const ConsensusFrontier&>(pre_rhs);
  switch (type) {
    case rocksdb::UpdateUserValueType::kLargest:
      op_id_.MakeAtLeast(rhs.op_id_);
      ht_.MakeAtLeast(rhs.ht_);
      return;
    case rocksdb::UpdateUserValueType::kSmallest:
      op_id_.MakeAtMost(rhs.op_id_);
      ht_.MakeAtMost(rhs.ht_);
      return;
  }
  FATAL_INVALID_ENUM_VALUE(rocksdb::UpdateUserValueType, type);
}

} // namespace docdb
} // namespace yb
