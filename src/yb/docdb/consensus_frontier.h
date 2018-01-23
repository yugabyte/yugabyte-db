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

#ifndef YB_DOCDB_CONSENSUS_FRONTIER_H
#define YB_DOCDB_CONSENSUS_FRONTIER_H

#include "yb/rocksdb/metadata.h"

namespace yb {
namespace docdb {

// DocDB implementation of RocksDB UserFrontier. Sustains op id and hybrid time.
// The distinction from user boundary values is that here hybrid time is taken from applied
// Raft log entery, whereas user boundary values extract hybrid time from added values.
// This is important for transactions, because values would have commit time of transaction,
// but "apply intent" Raft log entries will have a later hybrid time.
class ConsensusFrontier : public rocksdb::UserFrontier {
 public:
  std::unique_ptr<UserFrontier> Clone() const override {
    return std::make_unique<ConsensusFrontier>(*this);
  }

  bool Equals(const UserFrontier& rhs) const override;
  std::string ToString() const override;
  void ToPB(google::protobuf::Any* pb) const override;
  void Update(const rocksdb::UserFrontier& rhs, rocksdb::UpdateUserValueType type) override;
  void FromPB(const google::protobuf::Any& pb) override;
  void FromOpIdPBDeprecated(const OpIdPB& pb) override;

  const OpId& op_id() const { return op_id_; }
  void set_op_id(const OpId& value) { op_id_ = value; }

  template <class PB>
  void set_op_id(const PB& pb) { op_id_ = OpId::FromPB(pb); }

  HybridTime hybrid_time() const { return ht_; }
  void set_hybrid_time(HybridTime value) { ht_ = value; }

 private:
  OpId op_id_;
  HybridTime ht_;
};

typedef rocksdb::UserFrontiersBase<ConsensusFrontier> ConsensusFrontiers;

inline void set_op_id(const OpId& value, ConsensusFrontiers* frontiers) {
  frontiers->Smallest().set_op_id(value);
  frontiers->Largest().set_op_id(value);
}

inline void set_hybrid_time(HybridTime value, ConsensusFrontiers* frontiers) {
  frontiers->Smallest().set_hybrid_time(value);
  frontiers->Largest().set_hybrid_time(value);
}

} // namespace docdb
} // namespace yb

#endif // YB_DOCDB_CONSENSUS_FRONTIER_H
