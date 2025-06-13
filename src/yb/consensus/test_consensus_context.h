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

#include <unordered_set>

#include "yb/consensus/consensus_context.h"

namespace yb {
namespace consensus {

class TestConsensusContext : public ConsensusContext {
 public:
  void SetPropagatedSafeTime(HybridTime ht) override {}

  void SetMvccPropagatedSafeTime(HybridTime ht) override {}

  bool ShouldApplyWrite() override { return true; }

  Result<HybridTime> PreparePeerRequest() override { return HybridTime(); }

  Status MajorityReplicated() override { return Status::OK(); }

  void ChangeConfigReplicated(const RaftConfigPB&) override {}

  uint64_t NumSSTFiles() override { return 0; }

  void ListenNumSSTFilesChanged(std::function<void()> listener) override {}

  Status CheckOperationAllowed(
      const OpId& op_id, consensus::OperationType op_type) override {
    return Status::OK();
  }
};

} // namespace consensus
} // namespace yb
