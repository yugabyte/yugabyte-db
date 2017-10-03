// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.

#include "kudu/client/scanner-internal.h"

#include <algorithm>
#include <boost/bind.hpp>
#include <cmath>
#include <string>
#include <vector>

#include "kudu/client/client-internal.h"
#include "kudu/client/meta_cache.h"
#include "kudu/client/row_result.h"
#include "kudu/client/table-internal.h"
#include "kudu/common/wire_protocol.h"
#include "kudu/gutil/strings/substitute.h"
#include "kudu/rpc/rpc_controller.h"
#include "kudu/util/hexdump.h"

using std::set;
using std::string;

namespace kudu {

using rpc::RpcController;
using strings::Substitute;
using strings::SubstituteAndAppend;
using tserver::ColumnRangePredicatePB;
using tserver::NewScanRequestPB;
using tserver::ScanResponsePB;

namespace client {

using internal::RemoteTabletServer;

static const int64_t kNoTimestamp = -1;

KuduScanner::Data::Data(KuduTable* table)
  : open_(false),
    data_in_open_(false),
    has_batch_size_bytes_(false),
    batch_size_bytes_(0),
    selection_(KuduClient::CLOSEST_REPLICA),
    read_mode_(READ_LATEST),
    is_fault_tolerant_(false),
    snapshot_timestamp_(kNoTimestamp),
    table_(DCHECK_NOTNULL(table)),
    arena_(1024, 1024*1024),
    spec_encoder_(table->schema().schema_, &arena_),
    timeout_(MonoDelta::FromMilliseconds(kScanTimeoutMillis)),
    scan_attempts_(0) {
  SetProjectionSchema(table->schema().schema_);
}

KuduScanner::Data::~Data() {
}

Status KuduScanner::Data::CheckForErrors() {
  if (PREDICT_TRUE(!last_response_.has_error())) {
    return Status::OK();
  }

  return StatusFromPB(last_response_.error().status());
}

void KuduScanner::Data::CopyPredicateBound(const ColumnSchema& col,
                                           const void* bound_src,
                                           string* bound_dst) {
  const void* src;
  size_t size;
  if (col.type_info()->physical_type() == BINARY) {
    // Copying a string involves an extra level of indirection through its
    // owning slice.
    const Slice* s = reinterpret_cast<const Slice*>(bound_src);
    src = s->data();
    size = s->size();
  } else {
    src = bound_src;
    size = col.type_info()->size();
  }
  bound_dst->assign(reinterpret_cast<const char*>(src), size);
}

Status KuduScanner::Data::CanBeRetried(const bool isNewScan,
                                       const Status& rpc_status, const Status& server_status,
                                       const MonoTime& actual_deadline, const MonoTime& deadline,
                                       const vector<RemoteTabletServer*>& candidates,
                                       set<string>* blacklist) {
  CHECK(!rpc_status.ok() || !server_status.ok());

  // Check for ERROR_SERVER_TOO_BUSY, which should result in a retry after a delay.
  if (server_status.ok() &&
      !rpc_status.ok() &&
      controller_.error_response() &&
      controller_.error_response()->code() == rpc::ErrorStatusPB::ERROR_SERVER_TOO_BUSY) {
    UpdateLastError(rpc_status);

    // Exponential backoff with jitter anchored between 10ms and 20ms, and an
    // upper bound between 2.5s and 5s.
    MonoDelta sleep = MonoDelta::FromMilliseconds(
        (10 + rand() % 10) * static_cast<int>(std::pow(2.0, std::min(8, scan_attempts_ - 1))));
    MonoTime now = MonoTime::Now(MonoTime::FINE);
    now.AddDelta(sleep);
    if (deadline.ComesBefore(now)) {
      Status ret = Status::TimedOut("unable to retry before timeout",
                                    rpc_status.ToString());
      return last_error_.ok() ?
          ret : ret.CloneAndAppend(last_error_.ToString());
    }
    LOG(INFO) << "Retrying scan to busy tablet server " << ts_->ToString()
              << " after " << sleep.ToString() << "; attempt " << scan_attempts_;
    SleepFor(sleep);
    return Status::OK();
  }

  // Start by checking network errors.
  if (!rpc_status.ok()) {
    if (rpc_status.IsTimedOut() && actual_deadline.Equals(deadline)) {
      // If we ended because of the overall deadline, we're done.
      // We didn't wait a full RPC timeout though, so don't mark the tserver as failed.
      LOG(INFO) << "Scan of tablet " << remote_->tablet_id() << " at "
          << ts_->ToString() << " deadline expired.";
      return last_error_.ok()
          ? rpc_status : rpc_status.CloneAndAppend(last_error_.ToString());
    } else {
      // All other types of network errors are retriable, and also indicate the tserver is failed.
      UpdateLastError(rpc_status);
      table_->client()->data_->meta_cache_->MarkTSFailed(ts_, rpc_status);
    }
  }

  // If we're in the middle of a batch and doing a non fault-tolerant scan, then
  // we cannot retry. Non fault-tolerant scans can still be retried on a tablet
  // boundary (i.e. an OpenTablet call).
  if (!isNewScan && !is_fault_tolerant_) {
    return !rpc_status.ok() ? rpc_status : server_status;
  }

  // For retries, the correct action depends on the particular failure condition.
  //
  // On an RPC error, we retry at a different tablet server.
  //
  // If the server returned an error code, it depends:
  //
  //   - SCANNER_EXPIRED    : The scan can be retried at the same tablet server.
  //
  //   - TABLET_NOT_RUNNING : The scan can be retried at a different tablet server, subject
  //                          to the client's specified selection criteria.
  //
  //   - TABLET_NOT_FOUND   : The scan can be retried at a different tablet server, subject
  //                          to the client's specified selection criteria.
  //                          The metadata for this tablet should be refreshed.
  //
  //   - Any other error    : Fatal. This indicates an unexpected error while processing the scan
  //                          request.
  if (rpc_status.ok() && !server_status.ok()) {
    UpdateLastError(server_status);

    const tserver::TabletServerErrorPB& error = last_response_.error();
    switch (error.code()) {
      case tserver::TabletServerErrorPB::SCANNER_EXPIRED:
        VLOG(1) << "Got SCANNER_EXPIRED error code, non-fatal error.";
        break;
      case tserver::TabletServerErrorPB::TABLET_NOT_RUNNING:
        VLOG(1) << "Got error code " << tserver::TabletServerErrorPB::Code_Name(error.code())
            << ": temporarily blacklisting node " << ts_->permanent_uuid();
        blacklist->insert(ts_->permanent_uuid());
        // We've blacklisted all the live candidate tservers.
        // Do a short random sleep, clear the temp blacklist, then do another round of retries.
        if (!candidates.empty() && candidates.size() == blacklist->size()) {
          MonoDelta sleep_delta = MonoDelta::FromMilliseconds((random() % 5000) + 1000);
          LOG(INFO) << "All live candidate nodes are unavailable because of transient errors."
              << " Sleeping for " << sleep_delta.ToMilliseconds() << " ms before trying again.";
          SleepFor(sleep_delta);
          blacklist->clear();
        }
        break;
      case tserver::TabletServerErrorPB::TABLET_NOT_FOUND: {
        // There was either a tablet configuration change or the table was
        // deleted, since at the time of this writing we don't support splits.
        // Backoff, then force a re-fetch of the tablet metadata.
        remote_->MarkStale();
        // TODO: Only backoff on the second time we hit TABLET_NOT_FOUND on the
        // same tablet (see KUDU-1314).
        MonoDelta backoff_time = MonoDelta::FromMilliseconds((random() % 1000) + 500);
        SleepFor(backoff_time);
        break;
      }
      default:
        // All other server errors are fatal. Usually indicates a malformed request, e.g. a bad scan
        // specification.
        return server_status;
    }
  }

  return Status::OK();
}

Status KuduScanner::Data::OpenTablet(const string& partition_key,
                                     const MonoTime& deadline,
                                     set<string>* blacklist) {

  PrepareRequest(KuduScanner::Data::NEW);
  next_req_.clear_scanner_id();
  NewScanRequestPB* scan = next_req_.mutable_new_scan_request();
  switch (read_mode_) {
    case READ_LATEST: scan->set_read_mode(kudu::READ_LATEST); break;
    case READ_AT_SNAPSHOT: scan->set_read_mode(kudu::READ_AT_SNAPSHOT); break;
    default: LOG(FATAL) << "Unexpected read mode.";
  }

  if (is_fault_tolerant_) {
    scan->set_order_mode(kudu::ORDERED);
  } else {
    scan->set_order_mode(kudu::UNORDERED);
  }

  if (last_primary_key_.length() > 0) {
    VLOG(1) << "Setting NewScanRequestPB last_primary_key to hex value "
        << HexDump(last_primary_key_);
    scan->set_last_primary_key(last_primary_key_);
  }

  scan->set_cache_blocks(spec_.cache_blocks());

  if (snapshot_timestamp_ != kNoTimestamp) {
    if (PREDICT_FALSE(read_mode_ != READ_AT_SNAPSHOT)) {
      LOG(WARNING) << "Scan snapshot timestamp set but read mode was READ_LATEST."
          " Ignoring timestamp.";
    } else {
      scan->set_snap_timestamp(snapshot_timestamp_);
    }
  }

  // Set up the predicates.
  scan->clear_range_predicates();
  for (const ColumnRangePredicate& pred : spec_.predicates()) {
    const ColumnSchema& col = pred.column();
    const ValueRange& range = pred.range();
    ColumnRangePredicatePB* pb = scan->add_range_predicates();
    if (range.has_lower_bound()) {
      CopyPredicateBound(col, range.lower_bound(),
                         pb->mutable_lower_bound());
    }
    if (range.has_upper_bound()) {
      CopyPredicateBound(col, range.upper_bound(),
                         pb->mutable_upper_bound());
    }
    ColumnSchemaToPB(col, pb->mutable_column());
  }

  if (spec_.lower_bound_key()) {
    scan->mutable_start_primary_key()->assign(
      reinterpret_cast<const char*>(spec_.lower_bound_key()->encoded_key().data()),
      spec_.lower_bound_key()->encoded_key().size());
  } else {
    scan->clear_start_primary_key();
  }
  if (spec_.exclusive_upper_bound_key()) {
    scan->mutable_stop_primary_key()->assign(
      reinterpret_cast<const char*>(spec_.exclusive_upper_bound_key()->encoded_key().data()),
      spec_.exclusive_upper_bound_key()->encoded_key().size());
  } else {
    scan->clear_stop_primary_key();
  }
  RETURN_NOT_OK(SchemaToColumnPBs(*projection_, scan->mutable_projected_columns(),
                                  SCHEMA_PB_WITHOUT_STORAGE_ATTRIBUTES | SCHEMA_PB_WITHOUT_IDS));

  for (int attempt = 1;; attempt++) {
    Synchronizer sync;
    table_->client()->data_->meta_cache_->LookupTabletByKey(table_,
                                                            partition_key,
                                                            deadline,
                                                            &remote_,
                                                            sync.AsStatusCallback());
    RETURN_NOT_OK(sync.Wait());

    scan->set_tablet_id(remote_->tablet_id());

    RemoteTabletServer *ts;
    vector<RemoteTabletServer*> candidates;
    Status lookup_status = table_->client()->data_->GetTabletServer(
        table_->client(),
        remote_,
        selection_,
        *blacklist,
        &candidates,
        &ts);
    // If we get ServiceUnavailable, this indicates that the tablet doesn't
    // currently have any known leader. We should sleep and retry, since
    // it's likely that the tablet is undergoing a leader election and will
    // soon have one.
    if (lookup_status.IsServiceUnavailable() &&
        MonoTime::Now(MonoTime::FINE).ComesBefore(deadline)) {
      int sleep_ms = attempt * 100;
      VLOG(1) << "Tablet " << remote_->tablet_id() << " current unavailable: "
              << lookup_status.ToString() << ". Sleeping for " << sleep_ms << "ms "
              << "and retrying...";
      SleepFor(MonoDelta::FromMilliseconds(sleep_ms));
      continue;
    }
    RETURN_NOT_OK(lookup_status);

    MonoTime now = MonoTime::Now(MonoTime::FINE);
    if (deadline.ComesBefore(now)) {
      Status ret = Status::TimedOut("Scan timed out, deadline expired");
      return last_error_.ok() ?
          ret : ret.CloneAndAppend(last_error_.ToString());
    }

    // Recalculate the deadlines.
    // If we have other replicas beyond this one to try, then we'll try to
    // open the scanner with the default RPC timeout. That gives us time to
    // try other replicas later. Otherwise, we open the scanner using the
    // full remaining deadline for the user's call.
    MonoTime rpc_deadline;
    if (static_cast<int>(candidates.size()) - blacklist->size() > 1) {
      rpc_deadline = now;
      rpc_deadline.AddDelta(table_->client()->default_rpc_timeout());
      rpc_deadline = MonoTime::Earliest(deadline, rpc_deadline);
    } else {
      rpc_deadline = deadline;
    }

    controller_.Reset();
    controller_.set_deadline(rpc_deadline);

    CHECK(ts->proxy());
    ts_ = CHECK_NOTNULL(ts);
    proxy_ = ts->proxy();
    const Status rpc_status = proxy_->Scan(next_req_, &last_response_, &controller_);
    const Status server_status = CheckForErrors();
    if (rpc_status.ok() && server_status.ok()) {
      scan_attempts_ = 0;
      break;
    }
    scan_attempts_++;
    RETURN_NOT_OK(CanBeRetried(true, rpc_status, server_status, rpc_deadline, deadline,
                               candidates, blacklist));
  }

  next_req_.clear_new_scan_request();
  data_in_open_ = last_response_.has_data();
  if (last_response_.has_more_results()) {
    next_req_.set_scanner_id(last_response_.scanner_id());
    VLOG(1) << "Opened tablet " << remote_->tablet_id()
            << ", scanner ID " << last_response_.scanner_id();
  } else if (last_response_.has_data()) {
    VLOG(1) << "Opened tablet " << remote_->tablet_id() << ", no scanner ID assigned";
  } else {
    VLOG(1) << "Opened tablet " << remote_->tablet_id() << " (no rows), no scanner ID assigned";
  }

  // If present in the response, set the snapshot timestamp and the encoded last
  // primary key.  This is used when retrying the scan elsewhere.  The last
  // primary key is also updated on each scan response.
  if (is_fault_tolerant_) {
    CHECK(last_response_.has_snap_timestamp());
    snapshot_timestamp_ = last_response_.snap_timestamp();
    if (last_response_.has_last_primary_key()) {
      last_primary_key_ = last_response_.last_primary_key();
    }
  }

  if (last_response_.has_snap_timestamp()) {
    table_->client()->data_->UpdateLatestObservedTimestamp(last_response_.snap_timestamp());
  }

  return Status::OK();
}

Status KuduScanner::Data::KeepAlive() {
  if (!open_) return Status::IllegalState("Scanner was not open.");
  // If there is no scanner to keep alive, we still return Status::OK().
  if (!last_response_.IsInitialized() || !last_response_.has_more_results() ||
      !next_req_.has_scanner_id()) {
    return Status::OK();
  }

  RpcController controller;
  controller.set_timeout(timeout_);
  tserver::ScannerKeepAliveRequestPB request;
  request.set_scanner_id(next_req_.scanner_id());
  tserver::ScannerKeepAliveResponsePB response;
  RETURN_NOT_OK(proxy_->ScannerKeepAlive(request, &response, &controller));
  if (response.has_error()) {
    return StatusFromPB(response.error().status());
  }
  return Status::OK();
}

bool KuduScanner::Data::MoreTablets() const {
  CHECK(open_);
  // TODO(KUDU-565): add a test which has a scan end on a tablet boundary

  if (remote_->partition().partition_key_end().empty()) {
    // Last tablet -- nothing more to scan.
    return false;
  }

  if (!spec_.exclusive_upper_bound_partition_key().empty() &&
      spec_.exclusive_upper_bound_partition_key() <= remote_->partition().partition_key_end()) {
    // We are not past the scan's upper bound partition key.
    return false;
  }

  if (!table_->partition_schema().IsSimplePKRangePartitioning(*table_->schema().schema_)) {
    // We can't do culling yet if the partitioning isn't simple.
    return true;
  }

  if (spec_.exclusive_upper_bound_key() == nullptr) {
    // No upper bound - keep going!
    return true;
  }

  // Otherwise, we have to compare the upper bound.
  return spec_.exclusive_upper_bound_key()->encoded_key()
          .compare(remote_->partition().partition_key_end()) > 0;
}

void KuduScanner::Data::PrepareRequest(RequestType state) {
  if (state == KuduScanner::Data::CLOSE) {
    next_req_.set_batch_size_bytes(0);
  } else if (has_batch_size_bytes_) {
    next_req_.set_batch_size_bytes(batch_size_bytes_);
  } else {
    next_req_.clear_batch_size_bytes();
  }

  if (state == KuduScanner::Data::NEW) {
    next_req_.set_call_seq_id(0);
  } else {
    next_req_.set_call_seq_id(next_req_.call_seq_id() + 1);
  }
}

void KuduScanner::Data::UpdateLastError(const Status& error) {
  if (last_error_.ok() || last_error_.IsTimedOut()) {
    last_error_ = error;
  }
}

void KuduScanner::Data::SetProjectionSchema(const Schema* schema) {
  projection_ = schema;
  client_projection_ = KuduSchema(*schema);
}



////////////////////////////////////////////////////////////
// KuduScanBatch
////////////////////////////////////////////////////////////

KuduScanBatch::Data::Data() : projection_(NULL) {}

KuduScanBatch::Data::~Data() {}

size_t KuduScanBatch::Data::CalculateProjectedRowSize(const Schema& proj) {
  return proj.byte_size() +
        (proj.has_nullables() ? BitmapSize(proj.num_columns()) : 0);
}

Status KuduScanBatch::Data::Reset(RpcController* controller,
                                  const Schema* projection,
                                  const KuduSchema* client_projection,
                                  gscoped_ptr<RowwiseRowBlockPB> data) {
  CHECK(controller->finished());
  controller_.Swap(controller);
  projection_ = projection;
  client_projection_ = client_projection;
  resp_data_.Swap(data.get());

  // First, rewrite the relative addresses into absolute ones.
  if (PREDICT_FALSE(!resp_data_.has_rows_sidecar())) {
    return Status::Corruption("Server sent invalid response: no row data");
  } else {
    Status s = controller_.GetSidecar(resp_data_.rows_sidecar(), &direct_data_);
    if (!s.ok()) {
      return Status::Corruption("Server sent invalid response: row data "
                                "sidecar index corrupt", s.ToString());
    }
  }

  if (resp_data_.has_indirect_data_sidecar()) {
    Status s = controller_.GetSidecar(resp_data_.indirect_data_sidecar(),
                                      &indirect_data_);
    if (!s.ok()) {
      return Status::Corruption("Server sent invalid response: indirect data "
                                "sidecar index corrupt", s.ToString());
    }
  }

  RETURN_NOT_OK(RewriteRowBlockPointers(*projection_, resp_data_, indirect_data_, &direct_data_));
  projected_row_size_ = CalculateProjectedRowSize(*projection_);
  return Status::OK();
}

void KuduScanBatch::Data::ExtractRows(vector<KuduScanBatch::RowPtr>* rows) {
  int n_rows = resp_data_.num_rows();
  rows->resize(n_rows);

  if (PREDICT_FALSE(n_rows == 0)) {
    // Early-out here to avoid a UBSAN failure.
    VLOG(1) << "Extracted 0 rows";
    return;
  }

  // Initialize each RowPtr with data from the response.
  //
  // Doing this resize and array indexing turns out to be noticeably faster
  // than using reserve and push_back.
  const uint8_t* src = direct_data_.data();
  KuduScanBatch::RowPtr* dst = &(*rows)[0];
  while (n_rows > 0) {
    *dst = KuduScanBatch::RowPtr(projection_, client_projection_,src);
    dst++;
    src += projected_row_size_;
    n_rows--;
  }
  VLOG(1) << "Extracted " << rows->size() << " rows";
}

void KuduScanBatch::Data::Clear() {
  resp_data_.Clear();
  controller_.Reset();
}

} // namespace client
} // namespace kudu
