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
#include "yb/client/client.h"

#include "yb/consensus/opid_util.h"
#include "yb/util/logging.h"
#include "yb/util/threadpool.h"

DEFINE_int32(async_replication_polling_delay_ms, 0,
             "How long to delay in ms between applying and repolling.");
DEFINE_int32(replication_failure_delay_exponent, 16 /* ~ 2^16/1000 ~= 65 sec */,
             "Max number of failures (N) to use when calculating exponential backoff (2^N-1).");

DECLARE_int32(cdc_rpc_timeout_ms);

namespace yb {
namespace tserver {
namespace enterprise {

CDCPoller::CDCPoller(const cdc::ProducerTabletInfo& producer_tablet_info,
                     const cdc::ConsumerTabletInfo& consumer_tablet_info,
                     std::function<bool(void)> should_continue_polling,
                     std::function<cdc::CDCServiceProxy*(void)> get_proxy,
                     std::function<void(void)> remove_self_from_pollers_map,
                     ThreadPool* thread_pool,
                     const std::shared_ptr<client::YBClient>& client,
                     CDCConsumer* cdc_consumer) :
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
        client,
        std::bind(&CDCPoller::HandleApplyChanges, this, std::placeholders::_1))),
    thread_pool_(thread_pool),
    cdc_consumer_(cdc_consumer) {}

CDCPoller::~CDCPoller() {
  output_client_->Shutdown();
}

std::string CDCPoller::ToString() const {
  std::ostringstream os;
  os << "P " << producer_tablet_info_.stream_id << ":" << producer_tablet_info_.tablet_id
     << " C " << consumer_tablet_info_.table_id << ":" << consumer_tablet_info_.tablet_id;
  return os.str();
}

bool CDCPoller::CheckOnline() {
  return cdc_consumer_ != nullptr;
}

#define RETURN_WHEN_OFFLINE() \
  if (!CheckOnline()) { \
    LOG(WARNING) << "CDC Poller went offline: " << ToString(); \
    return; \
  }

void CDCPoller::Poll() {
  RETURN_WHEN_OFFLINE();
  WARN_NOT_OK(thread_pool_->SubmitFunc(std::bind(&CDCPoller::DoPoll, this)),
              "Could not submit Poll to thread pool");
}

void CDCPoller::DoPoll() {
  RETURN_WHEN_OFFLINE();

  // determine if we should delay our upcoming poll
  if (FLAGS_async_replication_polling_delay_ms > 0 || poll_failures_ > 0) {
    int64_t delay = max(FLAGS_async_replication_polling_delay_ms, // user setting
                        (1 << poll_failures_) -1); // failure backoff
    SleepFor(MonoDelta::FromMilliseconds(delay));
  }

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
  RETURN_WHEN_OFFLINE();

  WARN_NOT_OK(thread_pool_->SubmitFunc(std::bind(&CDCPoller::DoHandlePoll, this)),
              "Could not submit HandlePoll to thread pool");
}

void CDCPoller::DoHandlePoll() {

  RETURN_WHEN_OFFLINE();

  if (!should_continue_polling_()) {
    return remove_self_from_pollers_map_();
  }

  bool failed = false;
  if (!rpc_->status().ok()) {
    LOG(INFO) << "CDCPoller failure: " << rpc_->status().ToString();
    failed = true;
  } else if (resp_->has_error()) {
    LOG(WARNING) << "CDCPoller failure response: " << resp_->error().status().DebugString();
    failed = true;
  } else if (!resp_->has_checkpoint()) {
    LOG(ERROR) << "CDCPoller failure: no checkpoint";
    failed = true;
  }
  if (failed) {
    // In case of errors, try polling again with backoff
    poll_failures_ = min(poll_failures_ + 1, FLAGS_replication_failure_delay_exponent);
    return Poll();
  }
  poll_failures_ = max(poll_failures_ - 2, 0); // otherwise, recover slowly if we're congested

  // Success Case: ApplyChanges() from Poll
  WARN_NOT_OK(output_client_->ApplyChanges(resp_.get()), "Could not ApplyChanges");
}

void CDCPoller::HandleApplyChanges(cdc::OutputClientResponse response) {
  RETURN_WHEN_OFFLINE();

  WARN_NOT_OK(thread_pool_->SubmitFunc(std::bind(&CDCPoller::DoHandleApplyChanges, this, response)),
              "Could not submit HandleApplyChanges to thread pool");
}

void CDCPoller::DoHandleApplyChanges(cdc::OutputClientResponse response) {
  RETURN_WHEN_OFFLINE();

  if (!should_continue_polling_()) {
    return remove_self_from_pollers_map_();
  }
  if (!response.status.ok()) {
    LOG(WARNING) << "ApplyChanges failure: " << response.status;
    // Repeat the ApplyChanges step, with exponential backoff
    apply_failures_ = min(apply_failures_ + 1, FLAGS_replication_failure_delay_exponent);
    int64_t delay = (1 << apply_failures_) -1;
    SleepFor(MonoDelta::FromMilliseconds(delay));
    WARN_NOT_OK(output_client_->ApplyChanges(resp_.get()), "Could not ApplyChanges");
    return;
  }
  apply_failures_ = max(apply_failures_ - 2, 0); // recover slowly if we've gotten congested

  op_id_ = response.last_applied_op_id;

  Poll();
}
#undef RETURN_WHEN_OFFLINE

} // namespace enterprise
} // namespace tserver
} // namespace yb
