// Copyright (c) YugabyteDB, Inc.
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

#include "yb/tserver/cdc_table_poller.h"

#include "yb/cdc/cdc_state_table.h"

#include "yb/common/wire_protocol.h"

#include "yb/tserver/cdc_table_output_client.h"

#include "yb/util/logging.h"
#include "yb/util/result.h"
#include "yb/util/status_format.h"

namespace yb::tserver {

CdcTablePoller::CdcTablePoller(
    const xcluster::ProducerTabletInfo& producer_tablet_info,
    const xcluster::ConsumerTabletInfo& consumer_tablet_info, const NamespaceId& namespace_id,
    ThreadPool* thread_pool, rpc::Rpcs* rpcs, client::YBClient& local_client,
    XClusterConsumer* xcluster_consumer, int64_t leader_term,
    std::function<int64_t(const TabletId&)> get_leader_term, const std::string& ts_uuid,
    cdc::CDCStateTable& cdc_state_table)
    : XClusterPoller(
          producer_tablet_info, consumer_tablet_info, namespace_id,
          /* auto_flags_version= */ nullptr, thread_pool, rpcs, local_client,
          /* source_client= */ nullptr, xcluster_consumer, leader_term, std::move(get_leader_term),
          /* is_automatic_mode= */ false, /* is_stream_paused= */ false, ts_uuid),
      cdc_state_table_(cdc_state_table) {}

void CdcTablePoller::InitCdcTablePoller(
    rocksdb::RateLimiter* rate_limiter,
    std::optional<std::unordered_set<TableId>> published_table_ids) {
  std::lock_guard l(data_mutex_);
  output_client_ = std::make_shared<CdcTableOutputClient>(
      this, consumer_tablet_info_.table_id, local_client_, rate_limiter,
      std::move(published_table_ids));
}

Result<CdcTablePoller::NextRequest> CdcTablePoller::ReadCheckpointFromCdcState() {
  auto entry = VERIFY_RESULT(cdc_state_table_.TryFetchEntry(
      {producer_tablet_info_.tablet_id, producer_tablet_info_.stream_id},
      cdc::CDCStateTableEntrySelector().IncludeCheckpoint().IncludeCDCSDKSafeTime()));
  // The stream is created with a checkpoint for each of its tablets.
  SCHECK(
      entry && entry->checkpoint && entry->checkpoint->valid(), IllegalState,
      "No checkpoint in cdc_state for tablet $0 of stream $1", producer_tablet_info_.tablet_id,
      producer_tablet_info_.stream_id);

  NextRequest next;
  next.checkpoint.set_term(entry->checkpoint->term);
  next.checkpoint.set_index(entry->checkpoint->index);
  next.checkpoint.set_key("");
  next.checkpoint.set_write_id(0);
  if (entry->cdc_sdk_safe_time) {
    next.safe_hybrid_time = *entry->cdc_sdk_safe_time;
  }
  return next;
}

Status CdcTablePoller::PrepareGetChangesRequest(cdc::GetChangesRequestPB* req) {
  if (!next_request_) {
    // Blocks, on a thread of the consumer's pool. Done once per poller.
    next_request_ = VERIFY_RESULT(ReadCheckpointFromCdcState());
    LOG_WITH_PREFIX(INFO) << "Polling from checkpoint "
                          << next_request_->checkpoint.ShortDebugString() << ", safe time "
                          << next_request_->safe_hybrid_time;
  }

  req->set_stream_id(producer_tablet_info_.stream_id.ToString());
  req->set_tablet_id(producer_tablet_info_.tablet_id);
  // Until the consumer moves this poller after a leader change, the old leader's CDC service
  // forwards the call to the new leader.
  req->set_serve_as_proxy(true);
  // The CDC table keeps the values undecoded, which is how the CDC service sends them to a
  // walsender, along with the attribute number and type that decoding needs.
  req->set_cdcsdk_request_source(cdc::CDCSDKRequestSource::WALSENDER);

  *req->mutable_from_cdc_sdk_checkpoint() = next_request_->checkpoint;
  req->set_safe_hybrid_time(next_request_->safe_hybrid_time);
  req->set_wal_segment_index(next_request_->wal_segment_index);
  req->set_max_index_in_sort_window(next_request_->max_index_in_sort_window);

  // Everything before the checkpoint has been written to the CDC table, so the stream no longer
  // needs it. The CDC service ignores the key and write id of an explicit checkpoint.
  auto* explicit_checkpoint = req->mutable_explicit_cdc_sdk_checkpoint();
  explicit_checkpoint->set_term(next_request_->checkpoint.term());
  explicit_checkpoint->set_index(next_request_->checkpoint.index());
  if (next_request_->safe_hybrid_time >= 0) {
    explicit_checkpoint->set_snapshot_time(next_request_->safe_hybrid_time);
  }
  return Status::OK();
}

Status CdcTablePoller::ProcessGetChangesResponseError(const cdc::GetChangesResponsePB& resp) {
  if (resp.has_error()) {
    // TODO: tablet splits. On TABLET_SPLIT the children have to be polled from this tablet's
    // checkpoint. Until then this poller keeps failing with backoff.
    LOG_WITH_PREFIX(WARNING) << "GetChanges failed: code="
                             << cdc::CDCErrorPB::Code_Name(resp.error().code())
                             << ", status=" << resp.error().status().ShortDebugString();
    auto status = StatusFromPB(resp.error().status());
    return status.ok() ? STATUS(InternalError, "GetChanges failed without a status") : status;
  }
  SCHECK(resp.has_cdc_sdk_checkpoint(), NotFound, "GetChanges response has no checkpoint");
  return Status::OK();
}

void CdcTablePoller::ChangesApplied(const XClusterOutputClientResponse& response) {
  const auto& resp = *response.get_changes_response;
  DCHECK(next_request_);
  NextRequest next;
  next.checkpoint = resp.cdc_sdk_checkpoint();
  // snapshot_time is not used when streaming changes.
  next.checkpoint.clear_snapshot_time();
  // The CDC service returns only changes that commit after the safe time of the request, so it
  // must not go back.
  next.safe_hybrid_time =
      resp.has_safe_hybrid_time() ? resp.safe_hybrid_time() : next_request_->safe_hybrid_time;
  next.wal_segment_index = resp.wal_segment_index();
  next.max_index_in_sort_window = resp.max_index_in_sort_window();
  next_request_ = std::move(next);

  // The CDC service has returned every change of this tablet that commits up to this time, since
  // it skips the changes that commit up to the safe time of the next request. All of them are in
  // the CDC table now. A transaction that is returned in parts does not move the time past its
  // commit time before its last part. The consumer publishes the time, see
  // XClusterConsumer::PublishXClusterSafeTime.
  if (next_request_->safe_hybrid_time > 0) {
    UpdateSafeTime(HybridTime(static_cast<uint64_t>(next_request_->safe_hybrid_time)));
  }
}

}  // namespace yb::tserver
