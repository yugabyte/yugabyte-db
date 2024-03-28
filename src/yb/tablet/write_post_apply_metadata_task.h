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

#include "yb/rpc/strand.h"

#include "yb/common/transaction.h"

#include "yb/tablet/tablet_fwd.h"

namespace yb::tablet {

// Used by TransactionParticipant to write apply op id to transaction metadata in intentsdb.
class WritePostApplyMetadataTask : public rpc::StrandTask {
 public:
  WritePostApplyMetadataTask(
      TransactionIntentApplier* applier,
      std::vector<PostApplyTransactionMetadata>&& metadatas,
      const std::string& log_prefix);

  void Prepare(std::shared_ptr<WritePostApplyMetadataTask> cleanup_task);
  void Run() override;
  void Done(const Status& status) override;

  virtual ~WritePostApplyMetadataTask() = default;

 private:
  const std::string& LogPrefix() const;

  TransactionIntentApplier& applier_;
  std::vector<PostApplyTransactionMetadata> metadatas_;
  const std::string& log_prefix_;

  std::shared_ptr<WritePostApplyMetadataTask> retain_self_;
};

} // namespace yb::tablet
