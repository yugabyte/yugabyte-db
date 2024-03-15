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

#include "yb/tablet/write_post_apply_metadata_task.h"

#include "yb/tablet/running_transaction_context.h"
#include "yb/tablet/transaction_intent_applier.h"
#include "yb/util/status_log.h"

namespace yb::tablet {

WritePostApplyMetadataTask::WritePostApplyMetadataTask(
    TransactionIntentApplier* applier,
    std::vector<PostApplyTransactionMetadata>&& metadatas,
    const std::string& log_prefix)
    : applier_(*applier), metadatas_(std::move(metadatas)), log_prefix_(log_prefix) {}

void WritePostApplyMetadataTask::Prepare(std::shared_ptr<WritePostApplyMetadataTask> self) {
  retain_self_ = std::move(self);
}

void WritePostApplyMetadataTask::Run() {
  auto status = applier_.WritePostApplyMetadata(std::move(metadatas_));
  WARN_NOT_OK(status, "Failed to write post-apply transaction metadata");
  VLOG(2) << "Wrote post-apply transaction metadata";
}

void WritePostApplyMetadataTask::Done(const Status& status) {
  if (!status.ok()) {
    YB_LOG_EVERY_N_SECS(WARNING, 1) << "Write metadata task failed: " << status;
  }
  retain_self_.reset();
}

const std::string& WritePostApplyMetadataTask::LogPrefix() const {
  return log_prefix_;
}

} // namespace yb::tablet
