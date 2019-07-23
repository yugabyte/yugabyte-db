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

#include "yb/tserver/cdc_poller.h"
#include "yb/tserver/cdc_consumer.h"
#include "yb/tserver/twodc_output_client.h"

#include "yb/cdc/cdc_service.pb.h"
#include "yb/cdc/cdc_service.proxy.h"

#include "yb/consensus/opid_util.h"
#include "yb/util/threadpool.h"

DEFINE_int32(async_replication_polling_delay_ms, 0,
             "How long to delay in ms between applying and repolling.");

DECLARE_int32(cdc_rpc_timeout_ms);

namespace yb {
namespace tserver {
namespace enterprise {

CDCPoller::CDCPoller(const cdc::ProducerTabletInfo& producer_tablet_info,
                     const cdc::ConsumerTabletInfo& consumer_tablet_info,
                     std::function<bool(void)> should_continue_polling,
                     std::function<cdc::CDCServiceProxy*(void)> get_proxy,
                     std::function<void(void)> remove_self_from_pollers_map,
                     ThreadPool* thread_pool) :
    producer_tablet_info_(producer_tablet_info),
    consumer_tablet_info_(consumer_tablet_info),
    should_continue_polling_(std::move(should_continue_polling)),
    get_proxy_(std::move(get_proxy)),
    remove_self_from_pollers_map_(std::move(remove_self_from_pollers_map)),
    op_id_(consensus::MinimumOpId()),
    resp_(std::make_unique<cdc::GetChangesResponsePB>()),
    rpc_(std::make_unique<rpc::RpcController>()),
    output_client_(CreateTwoDCOutputClient(
        consumer_tablet_info,
        std::bind(&CDCPoller::HandleApplyChanges, this, std::placeholders::_1))),
    thread_pool_(thread_pool) {}

void CDCPoller::Poll() {
  WARN_NOT_OK(thread_pool_->SubmitFunc(std::bind(&CDCPoller::DoPoll, this)),
              "Could not submit Poll to thread pool");
}

void CDCPoller::DoPoll() {
  cdc::GetChangesRequestPB req;
  req.set_stream_id(producer_tablet_info_.stream_id);
  req.set_tablet_id(producer_tablet_info_.tablet_id);

  cdc::CDCCheckpointPB checkpoint;
  *checkpoint.mutable_op_id() = op_id_;
  *req.mutable_from_checkpoint() = checkpoint;

  auto* proxy = get_proxy_();
  resp_ = std::make_unique<cdc::GetChangesResponsePB>();
  rpc_ = std::make_unique<rpc::RpcController>();
  rpc_->set_timeout(MonoDelta::FromMilliseconds(FLAGS_cdc_rpc_timeout_ms));
  proxy->GetChangesAsync(req, resp_.get(), rpc_.get(), std::bind(&CDCPoller::HandlePoll, this));
}

void CDCPoller::HandlePoll() {
  WARN_NOT_OK(thread_pool_->SubmitFunc(std::bind(&CDCPoller::DoHandlePoll, this)),
              "Could not submit HandlePoll to thread pool");
}

void CDCPoller::DoHandlePoll() {
  if (!should_continue_polling_()) {
    return remove_self_from_pollers_map_();
  }

  if (!rpc_->status().ok() || resp_->has_error()) {
    return Poll();
  }

  WARN_NOT_OK(output_client_->ApplyChanges(resp_.get()), "Could not ApplyChanges");
}

void CDCPoller::HandleApplyChanges(cdc::OutputClientResponse response) {
  WARN_NOT_OK(thread_pool_->SubmitFunc(std::bind(&CDCPoller::DoHandleApplyChanges, this, response)),
              "Could not submit HandleApplyChanges to thread pool");
}

void CDCPoller::DoHandleApplyChanges(cdc::OutputClientResponse response) {

  if (!should_continue_polling_()) {
    return remove_self_from_pollers_map_();
  }
  if (!response.status.ok()) {
    // Repeat the ApplyChanges step;
    WARN_NOT_OK(output_client_->ApplyChanges(resp_.get()), "Could not ApplyChanges");
    return;
  }

  op_id_ = response.last_applied_op_id;
  if (FLAGS_async_replication_polling_delay_ms != 0) {
    SleepFor(MonoDelta::FromMilliseconds(FLAGS_async_replication_polling_delay_ms));
  }

  Poll();
}

} // namespace enterprise
} // namespace tserver
} // namespace yb
