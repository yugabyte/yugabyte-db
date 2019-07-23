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

#include "yb/tserver/twodc_output_client.h"

#include "yb/cdc/cdc_consumer_util.h"

#include "yb/client/client.h"

namespace yb {
namespace tserver {
namespace enterprise {


class TwoDCOutputClient : public cdc::CDCOutputClient {
 public:
  TwoDCOutputClient(
      const cdc::ConsumerTabletInfo& consumer_tablet_info,
      std::function<void(const cdc::OutputClientResponse& response)> apply_changes_clbk) :
      consumer_tablet_info_(consumer_tablet_info),
      apply_changes_clbk_(std::move(apply_changes_clbk)) {}

  // TODO: Override with actual implementation.
  CHECKED_STATUS ApplyChanges(const cdc::GetChangesResponsePB* resp) override {
    cdc::OutputClientResponse client_resp;
    client_resp.status = Status::OK();
    client_resp.last_applied_op_id = resp->checkpoint().op_id();
    apply_changes_clbk_(client_resp);
    return Status::OK();
  }
 private:
  cdc::ConsumerTabletInfo consumer_tablet_info_;
  std::function<void(const cdc::OutputClientResponse& response)> apply_changes_clbk_;
};

std::unique_ptr<cdc::CDCOutputClient> CreateTwoDCOutputClient(
    const cdc::ConsumerTabletInfo& consumer_tablet_info,
    std::function<void(const cdc::OutputClientResponse& response)> apply_changes_clbk) {
  return std::make_unique<TwoDCOutputClient>(consumer_tablet_info, std::move(apply_changes_clbk));
}

} // namespace enterprise
} // namespace tserver
} // namespace yb
