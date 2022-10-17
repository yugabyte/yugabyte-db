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
#include "yb/client/client_fwd.h"
#include "yb/gutil/strings/split.h"
#include "yb/tserver/cdc_consumer.h"
#include "yb/tserver/twodc_output_client.h"

#include "yb/cdc/cdc_rpc.h"
#include "yb/cdc/cdc_service.pb.h"
#include "yb/cdc/cdc_service.proxy.h"
#include "yb/client/client.h"

#include "yb/consensus/opid_util.h"

#include "yb/gutil/strings/substitute.h"

#include "yb/gutil/dynamic_annotations.h"
#include "yb/util/flag_tags.h"
#include "yb/util/logging.h"
#include "yb/util/status_log.h"
#include "yb/util/threadpool.h"

// Similar heuristic to heartbeat_interval in heartbeater.cc.
DEFINE_int32(async_replication_polling_delay_ms, 0,
             "How long to delay in ms between applying and polling.");
TAG_FLAG(async_replication_polling_delay_ms, runtime);

DEFINE_int32(async_replication_idle_delay_ms, 100,
             "How long to delay between polling when we expect no data at the destination.");
TAG_FLAG(async_replication_idle_delay_ms, runtime);

DEFINE_int32(async_replication_max_idle_wait, 3,
             "Maximum number of consecutive empty GetChanges until the poller "
             "backs off to the idle interval, rather than immediately retrying.");
TAG_FLAG(async_replication_max_idle_wait, runtime);

DEFINE_int32(replication_failure_delay_exponent, 16 /* ~ 2^16/1000 ~= 65 sec */,
             "Max number of failures (N) to use when calculating exponential backoff (2^N-1).");
TAG_FLAG(replication_failure_delay_exponent, runtime);

DEFINE_bool(cdc_consumer_use_proxy_forwarding, false,
            "When enabled, read requests from the CDC Consumer that go to the wrong node are "
            "forwarded to the correct node by the Producer.");
TAG_FLAG(cdc_consumer_use_proxy_forwarding, runtime);

DEFINE_test_flag(
    int32, xcluster_simulated_lag_ms, 0,
    "Simulate lag in xcluster replication. Replication is paused if set to -1.");
TAG_FLAG(TEST_xcluster_simulated_lag_ms, runtime);
DEFINE_test_flag(
    string, xcluster_simulated_lag_tablet_filter, "",
    "Comma separated list of producer tablet ids. If non empty, simulate lag in only applied to "
    "this list of tablets.");
// Strings are usually not runtime safe but in our case its ok if we temporary read garbled data
TAG_FLAG(TEST_xcluster_simulated_lag_tablet_filter, runtime);

DEFINE_test_flag(bool, cdc_skip_replication_poll, false,
                 "If true, polling will be skipped.");

DECLARE_int32(cdc_read_rpc_timeout_ms);

using namespace std::placeholders;

namespace yb {
namespace tserver {
namespace enterprise {

CDCPoller::CDCPoller(
    const cdc::ProducerTabletInfo& producer_tablet_info,
    const cdc::ConsumerTabletInfo& consumer_tablet_info,
    ThreadPool* thread_pool,
    rpc::Rpcs* rpcs,
    const std::shared_ptr<CDCClient>& local_client,
    const std::shared_ptr<CDCClient>& producer_client,
    CDCConsumer* cdc_consumer,
    bool use_local_tserver,
    client::YBTablePtr global_transaction_status_table,
    bool enable_replicate_transaction_status_table)
    : producer_tablet_info_(producer_tablet_info),
      consumer_tablet_info_(consumer_tablet_info),
      op_id_(consensus::MinimumOpId()),
      validated_schema_version_(0),
      resp_(std::make_unique<cdc::GetChangesResponsePB>()),
      output_client_(CreateTwoDCOutputClient(
          cdc_consumer,
          consumer_tablet_info,
          producer_tablet_info,
          local_client,
          thread_pool,
          rpcs,
          std::bind(&CDCPoller::HandleApplyChanges, this, _1),
          use_local_tserver,
          global_transaction_status_table,
          enable_replicate_transaction_status_table)),
      producer_client_(producer_client),
      thread_pool_(thread_pool),
      rpcs_(rpcs),
      poll_handle_(rpcs_->InvalidHandle()),
      cdc_consumer_(cdc_consumer),
      producer_safe_time_(HybridTime::kInvalid) {}

CDCPoller::~CDCPoller() {
  VLOG(1) << "Destroying CDCPoller";
  DCHECK(shutdown_);
}

void CDCPoller::Shutdown() {
  // The poller is shutdown in two cases:
  // 1. The regular case where the poller is deleted via CDCConsumer's TriggerDeletionOfOldPollers.
  //    This happens when the stream is deleted or the consumer tablet leader changes.
  // 2. During CDCConsumer::Shutdown(). Note that in this scenario, we may still be processing a
  //    GetChanges request / handle callback, so we shutdown what we can here (note that
  //    thread_pool_ is shutdown before we shutdown the pollers, so that will force most
  //    codepaths to exit early), and then using shared_from_this, destroy the object once all
  //    callbacks are complete.
  DCHECK(!shutdown_);
  shutdown_ = true;

  rpc::RpcCommandPtr rpc_to_abort;
  {
    std::lock_guard<std::mutex> l(data_mutex_);
    output_client_->Shutdown();
    if (poll_handle_ != rpcs_->InvalidHandle()) {
      rpc_to_abort = *poll_handle_;
    }
  }
  if (rpc_to_abort) {
    rpc_to_abort->Abort();
  }
}

std::string CDCPoller::LogPrefixUnlocked() const {
  return strings::Substitute("P [$0:$1] C [$2:$3]: ",
                             producer_tablet_info_.stream_id,
                             producer_tablet_info_.tablet_id,
                             consumer_tablet_info_.table_id,
                             consumer_tablet_info_.tablet_id);
}

bool CDCPoller::CheckOffline() { return shutdown_.load(); }

#define RETURN_WHEN_OFFLINE() \
  if (CheckOffline()) { \
    LOG_WITH_PREFIX_UNLOCKED(WARNING) << "CDC Poller went offline"; \
    return; \
  }

#define ACQUIRE_MUTEX_IF_ONLINE() \
  RETURN_WHEN_OFFLINE(); \
  std::lock_guard<std::mutex> l(data_mutex_);

void CDCPoller::SetSchemaVersion(uint32_t cur_version) {
  RETURN_WHEN_OFFLINE();
  WARN_NOT_OK(thread_pool_->SubmitFunc(std::bind(&CDCPoller::DoSetSchemaVersion,
                                                 shared_from_this(), cur_version)),
              "Could not submit SetSchemaVersion to thread pool");
}

void CDCPoller::DoSetSchemaVersion(uint32_t cur_version) {
  ACQUIRE_MUTEX_IF_ONLINE();

  if (validated_schema_version_ < cur_version) {
    validated_schema_version_ = cur_version;
    // re-enable polling.
    if (!is_polling_) {
      is_polling_ = true;
      LOG(INFO) << "Restarting polling on " << producer_tablet_info_.tablet_id;
      WARN_NOT_OK(thread_pool_->SubmitFunc(std::bind(&CDCPoller::DoPoll, shared_from_this())),
                  "Could not submit Poll to thread pool");
    }
  }
}

HybridTime CDCPoller::GetSafeTime() const {
  SharedLock lock(safe_time_lock_);
  return producer_safe_time_;
}

cdc::ConsumerTabletInfo CDCPoller::GetConsumerTabletInfo() const { return consumer_tablet_info_; }

void CDCPoller::UpdateSafeTime(int64 new_time) {
  HybridTime new_hybrid_time(new_time);
  if (!new_hybrid_time.is_special()) {
    std::lock_guard l(safe_time_lock_);
    if (producer_safe_time_.is_special() || new_hybrid_time > producer_safe_time_) {
      producer_safe_time_ = new_hybrid_time;
    }
  }
}

void CDCPoller::Poll() {
  RETURN_WHEN_OFFLINE();
  WARN_NOT_OK(thread_pool_->SubmitFunc(std::bind(&CDCPoller::DoPoll, shared_from_this())),
              "Could not submit Poll to thread pool");
}

void CDCPoller::DoPoll() {
  ACQUIRE_MUTEX_IF_ONLINE();

  if (PREDICT_FALSE(FLAGS_TEST_cdc_skip_replication_poll)) {
    SleepFor(MonoDelta::FromMilliseconds(FLAGS_async_replication_idle_delay_ms));
    Poll();
    return;
  }

  // determine if we should delay our upcoming poll
  int64_t delay = GetAtomicFlag(&FLAGS_async_replication_polling_delay_ms);  // normal throttling.
  if (idle_polls_ >= GetAtomicFlag(&FLAGS_async_replication_max_idle_wait)) {
    delay = std::max(
        delay, (int64_t)GetAtomicFlag(&FLAGS_async_replication_idle_delay_ms));  // idle backoff.
  }
  if (poll_failures_ > 0) {
    delay = std::max(delay, (int64_t)1 << poll_failures_); // exponential backoff for failures.
  }
  if (delay > 0) {
    SleepFor(MonoDelta::FromMilliseconds(delay));
  }

  const auto xcluster_simulated_lag_ms = GetAtomicFlag(&FLAGS_TEST_xcluster_simulated_lag_ms);
  if (PREDICT_FALSE(xcluster_simulated_lag_ms != 0)) {
    auto flag_info =
        gflags::GetCommandLineFlagInfoOrDie("TEST_xcluster_simulated_lag_tablet_filter");
    const auto& tablet_filter = flag_info.current_value;
    const auto tablet_filter_list = strings::Split(tablet_filter, ",");
    if (tablet_filter.empty() || std::find(
                                     tablet_filter_list.begin(), tablet_filter_list.end(),
                                     producer_tablet_info_.tablet_id) != tablet_filter_list.end()) {
      auto delay = GetAtomicFlag(&FLAGS_async_replication_idle_delay_ms);
      if (xcluster_simulated_lag_ms > 0) {
        delay = xcluster_simulated_lag_ms;
      }

      SleepFor(MonoDelta::FromMilliseconds(delay));

      // If replication is paused skip the GetChanges call
      if (xcluster_simulated_lag_ms < 0) {
        return Poll();
      }
    }
  }

  cdc::GetChangesRequestPB req;
  req.set_stream_id(producer_tablet_info_.stream_id);
  req.set_tablet_id(producer_tablet_info_.tablet_id);
  req.set_serve_as_proxy(GetAtomicFlag(&FLAGS_cdc_consumer_use_proxy_forwarding));

  cdc::CDCCheckpointPB checkpoint;
  *checkpoint.mutable_op_id() = op_id_;
  if (checkpoint.op_id().index() > 0 || checkpoint.op_id().term() > 0) {
    // Only send non-zero checkpoints in request.
    // If we don't know the latest checkpoint, then CDC producer can use the checkpoint from
    // cdc_state table.
    // This is useful in scenarios where a new tablet peer becomes replication leader for a
    // producer tablet and is not aware of the last checkpoint.
    *req.mutable_from_checkpoint() = checkpoint;
  }

  poll_handle_ = rpcs_->Prepare();
  if (poll_handle_ == rpcs_->InvalidHandle()) {
    DCHECK(CheckOffline());
    LOG_WITH_PREFIX_UNLOCKED(WARNING) << "Unable to perform poll, rpcs_ is shutdown";
    return;
  }

  *poll_handle_ = CreateGetChangesCDCRpc(
      CoarseMonoClock::now() + MonoDelta::FromMilliseconds(FLAGS_cdc_read_rpc_timeout_ms),
      nullptr, /* RemoteTablet: will get this from 'req' */
      producer_client_->client.get(),
      &req,
      std::bind(&CDCPoller::HandlePoll, shared_from_this(), _1, _2));
  (**poll_handle_).SendRpc();
}

void CDCPoller::HandlePoll(const Status& status, cdc::GetChangesResponsePB&& resp) {
  rpc::RpcCommandPtr retained;
  {
    std::lock_guard<std::mutex> l(data_mutex_);
    retained = rpcs_->Unregister(&poll_handle_);
  }
  RETURN_WHEN_OFFLINE();
  auto new_resp = std::make_shared<cdc::GetChangesResponsePB>(std::move(resp));
  WARN_NOT_OK(
      thread_pool_->SubmitFunc(
          std::bind(&CDCPoller::DoHandlePoll, shared_from_this(), status, new_resp)),
      "Could not submit HandlePoll to thread pool");
}

void CDCPoller::DoHandlePoll(Status status, std::shared_ptr<cdc::GetChangesResponsePB> resp) {
  ACQUIRE_MUTEX_IF_ONLINE();

  status_ = status;
  resp_ = resp;

  bool failed = false;
  if (!status_.ok()) {
    LOG_WITH_PREFIX_UNLOCKED(INFO) << "CDCPoller failure: " << status_.ToString();
    failed = true;
  } else if (resp_->has_error()) {
    LOG_WITH_PREFIX_UNLOCKED(WARNING) << "CDCPoller failure response: code="
                                      << resp_->error().code()
                                      << ", status=" << resp->error().status().DebugString();
    failed = true;

    if (resp_->error().code() == cdc::CDCErrorPB::CHECKPOINT_TOO_OLD) {
      cdc_consumer_->StoreReplicationError(
        consumer_tablet_info_.tablet_id,
        producer_tablet_info_.stream_id,
        ReplicationErrorPb::REPLICATION_MISSING_OP_ID,
        "Unable to find expected op id on the producer");
    }
  } else if (!resp_->has_checkpoint()) {
    LOG_WITH_PREFIX_UNLOCKED(ERROR) << "CDCPoller failure: no checkpoint";
    failed = true;
  }
  if (failed) {
    // In case of errors, try polling again with backoff
    poll_failures_ =
        std::min(poll_failures_ + 1, GetAtomicFlag(&FLAGS_replication_failure_delay_exponent));
    return Poll();
  }
  poll_failures_ = std::max(poll_failures_ - 2, 0); // otherwise, recover slowly if we're congested

  // Success Case: ApplyChanges() from Poll
  WARN_NOT_OK(output_client_->ApplyChanges(resp_.get()), "Could not ApplyChanges");
}

void CDCPoller::HandleApplyChanges(cdc::OutputClientResponse response) {
  RETURN_WHEN_OFFLINE();
  WARN_NOT_OK(thread_pool_->SubmitFunc(
              std::bind(&CDCPoller::DoHandleApplyChanges, shared_from_this(), response)),
              "Could not submit HandleApplyChanges to thread pool");
}

void CDCPoller::DoHandleApplyChanges(cdc::OutputClientResponse response) {
  ACQUIRE_MUTEX_IF_ONLINE();

  if (!response.status.ok()) {
    LOG_WITH_PREFIX_UNLOCKED(WARNING) << "ApplyChanges failure: " << response.status;
    // Repeat the ApplyChanges step, with exponential backoff
    apply_failures_ =
        std::min(apply_failures_ + 1, GetAtomicFlag(&FLAGS_replication_failure_delay_exponent));
    int64_t delay = (1 << apply_failures_) -1;
    SleepFor(MonoDelta::FromMilliseconds(delay));
    WARN_NOT_OK(output_client_->ApplyChanges(resp_.get()), "Could not ApplyChanges");
    return;
  }
  apply_failures_ = std::max(apply_failures_ - 2, 0); // recover slowly if we've gotten congested

  op_id_ = response.last_applied_op_id;

  idle_polls_ = (response.processed_record_count == 0) ? idle_polls_ + 1 : 0;


  if (validated_schema_version_ < response.wait_for_version) {
    is_polling_ = false;
    validated_schema_version_ = response.wait_for_version - 1;
  } else {
    // Once all changes have been successfully applied we can update the safe time
    UpdateSafeTime(resp_->safe_hybrid_time());

    Poll();
  }
}

#undef ACQUIRE_MUTEX_IF_ONLINE
#undef RETURN_WHEN_OFFLINE

} // namespace enterprise
} // namespace tserver
} // namespace yb
