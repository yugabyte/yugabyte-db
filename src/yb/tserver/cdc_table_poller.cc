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
    std::optional<std::unordered_set<TableId>> published_table_ids,
    std::optional<cdc::CdcTablePollerPositionPB> start_position,
    std::vector<TabletId> split_ancestor_tablet_ids) {
  std::lock_guard l(data_mutex_);
  start_position_ = std::move(start_position);
  split_ancestor_tablet_ids_ = std::move(split_ancestor_tablet_ids);
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

  for (const auto& ancestor_tablet_id : split_ancestor_tablet_ids_) {
    auto ancestor_entry = VERIFY_RESULT(cdc_state_table_.TryFetchEntry(
        {ancestor_tablet_id, producer_tablet_info_.stream_id},
        cdc::CDCStateTableEntrySelector().IncludeCheckpoint().IncludeCDCSDKSafeTime()));
    // The master deletes the row of a split tablet once both its children have been polled, and
    // the checkpoint is OpId::Max in a row that is being removed from the stream.
    if (!ancestor_entry || !ancestor_entry->checkpoint || !ancestor_entry->checkpoint->valid() ||
        *ancestor_entry->checkpoint == OpId::Max()) {
      continue;
    }
    // The tablet has a copy of the WAL of each of them, so their checkpoints are positions in its
    // own WAL. Everything up to them is in the CDC table: a row holds what a poller has applied.
    if (*ancestor_entry->checkpoint > OpId(next.checkpoint.term(), next.checkpoint.index())) {
      LOG_WITH_PREFIX(INFO) << "Polling from the checkpoint " << *ancestor_entry->checkpoint
                            << " of split tablet " << ancestor_tablet_id
                            << ", which is ahead of the tablet's own, "
                            << next.checkpoint.ShortDebugString();
      next.checkpoint.set_term(ancestor_entry->checkpoint->term);
      next.checkpoint.set_index(ancestor_entry->checkpoint->index);
    }
    if (ancestor_entry->cdc_sdk_safe_time) {
      next.safe_hybrid_time = std::max(
          next.safe_hybrid_time, static_cast<int64_t>(*ancestor_entry->cdc_sdk_safe_time));
    }
  }
  return next;
}

std::optional<cdc::CdcTablePollerPositionPB> CdcTablePoller::GetSplitPosition() const {
  std::lock_guard l(split_position_lock_);
  return split_position_;
}

Status CdcTablePoller::PrepareGetChangesRequest(cdc::GetChangesRequestPB* req) {
  if (!next_request_) {
    // Blocks, on a thread of the consumer's pool. Done once per poller.
    auto next = VERIFY_RESULT(ReadCheckpointFromCdcState());
    if (start_position_) {
      // Either the tablet came out of a split. Its row in cdc_state is a copy of the parent's row
      // from when the split was registered, and the parent's poller has got further since. The
      // tablet has a copy of the parent's WAL, so this poller continues from where that one
      // stopped, or from its own row once that is ahead. Starting any later would skip the changes
      // that the parent's poller had not read yet when it was told about the split.
      //
      // Or its table was added to a publication, and the position is only a safe time, just below
      // the commit time of that change. The row in cdc_state is from before it, and the CDC service
      // leaves out the changes that commit up to the safe time of the request.
      const OpId start_op_id(start_position_->term(), start_position_->index());
      if (start_op_id > OpId(next.checkpoint.term(), next.checkpoint.index())) {
        next.checkpoint.set_term(start_op_id.term);
        next.checkpoint.set_index(start_op_id.index);
      }
      next.safe_hybrid_time = std::max(next.safe_hybrid_time, start_position_->safe_hybrid_time());
    }
    next_request_ = std::move(next);
    LOG_WITH_PREFIX(INFO) << "Polling from checkpoint "
                          << next_request_->checkpoint.ShortDebugString() << ", safe time "
                          << next_request_->safe_hybrid_time
                          << (start_position_
                                  ? Format(", given the start position $0",
                                           start_position_->ShortDebugString())
                                  : "");
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
  // The CDC service writes the checkpoint to cdc_state only every
  // cdc_state_checkpoint_update_interval_ms. Have it written right away for the position this
  // poller starts from. For a child of a split that is the position inherited from its parent,
  // which a later poller of the tablet must not fall behind, and the write is also what tells the
  // master that the child has been polled, so that it can delete the parent.
  req->set_force_update_cdc_state_checkpoint(!applied_a_response_);
  return Status::OK();
}

Status CdcTablePoller::ProcessGetChangesResponseError(const cdc::GetChangesResponsePB& resp) {
  if (resp.has_error() && resp.error().code() == cdc::CDCErrorPB::TABLET_SPLIT) {
    // The CDC service says so as soon as the tablet is marked split, wherever this poller is in
    // the tablet's WAL, and returns nothing more for the tablet. The changes this poller has not
    // read are read from the children. Until the master has replaced the tablet with them, this
    // poller stays, failing with backoff: its safe time is what keeps the CDC safe time of the
    // database from passing those changes.
    DCHECK(next_request_);
    std::lock_guard l(split_position_lock_);
    if (!split_position_) {
      split_position_.emplace();
      split_position_->set_term(next_request_->checkpoint.term());
      split_position_->set_index(next_request_->checkpoint.index());
      split_position_->set_safe_hybrid_time(next_request_->safe_hybrid_time);
      LOG_WITH_PREFIX(INFO) << "The tablet has been split. Its children continue from "
                            << split_position_->ShortDebugString();
    }
    return STATUS_FORMAT(
        TryAgain, "Tablet $0 has been split, waiting for the pollers of its children to take over",
        producer_tablet_info_.tablet_id);
  }
  if (resp.has_error()) {
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
  applied_a_response_ = true;

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
