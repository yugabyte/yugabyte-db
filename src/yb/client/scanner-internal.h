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
#ifndef YB_CLIENT_SCANNER_INTERNAL_H
#define YB_CLIENT_SCANNER_INTERNAL_H

#include <set>
#include <string>
#include <vector>

#include "yb/gutil/macros.h"
#include "yb/client/client.h"
#include "yb/client/row_result.h"
#include "yb/common/scan_spec.h"
#include "yb/common/predicate_encoder.h"
#include "yb/tserver/tserver_service.proxy.h"

namespace yb {

namespace client {

class YBScanner::Data {
 public:
  explicit Data(YBTable* table);
  ~Data();

  CHECKED_STATUS CheckForErrors();

  // Copies a predicate lower or upper bound from 'bound_src' into
  // 'bound_dst'.
  void CopyPredicateBound(const ColumnSchema& col,
                          const void* bound_src, std::string* bound_dst);

  // Called when YBScanner::NextBatch or YBScanner::Data::OpenTablet result in an RPC or
  // server error. Returns the error status if the call cannot be retried.
  //
  // The number of parameters reflects the complexity of handling retries.
  // We must respect the overall scan 'deadline', as well as the 'blacklist' of servers
  // experiencing transient failures. See the implementation for more details.
  CHECKED_STATUS CanBeRetried(const bool isNewScan,
                      const Status& rpc_status,
                      const Status& server_status,
                      const MonoTime& actual_deadline,
                      const MonoTime& deadline,
                      const std::vector<internal::RemoteTabletServer*>& candidates,
                      std::set<std::string>* blacklist);

  // Open a tablet.
  // The deadline is the time budget for this operation.
  // The blacklist is used to temporarily filter out nodes that are experiencing transient errors.
  // This blacklist may be modified by the callee.
  CHECKED_STATUS OpenTablet(const std::string& partition_key,
                    const MonoTime& deadline,
                    std::set<std::string>* blacklist);

  CHECKED_STATUS KeepAlive();

  // Returns whether there exist more tablets we should scan.
  //
  // Note: there may not be any actual matching rows in subsequent tablets,
  // but we won't know until we scan them.
  bool MoreTablets() const;

  // Possible scan requests.
  enum RequestType {
    // A new scan of a particular tablet.
    NEW,

    // A continuation of an existing scan (to read more rows).
    CONTINUE,

    // A close of a partially-completed scan. Complete scans are closed
    // automatically by the tablet server.
    CLOSE
  };

  // Modifies fields in 'next_req_' in preparation for a new request.
  void PrepareRequest(RequestType state);

  // Update 'last_error_' if need be. Should be invoked whenever a
  // non-fatal (i.e. retriable) scan error is encountered.
  void UpdateLastError(const Status& error);

  // Sets the projection schema.
  void SetProjectionSchema(const Schema* schema);

  bool open_;
  bool data_in_open_;
  bool has_batch_size_bytes_;
  uint32 batch_size_bytes_;
  YBClient::ReplicaSelection selection_;

  ReadMode read_mode_;
  bool is_fault_tolerant_;
  int64_t snapshot_hybrid_time_;

  // The encoded last primary key from the most recent tablet scan response.
  std::string last_primary_key_;

  internal::RemoteTabletServer* ts_;
  // The proxy can be derived from the RemoteTabletServer, but this involves retaking the
  // meta cache lock. Keeping our own shared_ptr avoids this overhead.
  std::shared_ptr<tserver::TabletServerServiceProxy> proxy_;

  // The next scan request to be sent. This is cached as a field
  // since most scan requests will share the scanner ID with the previous
  // request.
  tserver::ScanRequestPB next_req_;

  // The last response received from the server. Cached for buffer reuse.
  tserver::ScanResponsePB last_response_;

  // RPC controller for the last in-flight RPC.
  rpc::RpcController controller_;

  // The table we're scanning.
  YBTable* table_;

  // The projection schema used in the scan.
  const Schema* projection_;

  // 'projection_' after it is converted to YBSchema, so that users can obtain
  // the projection without having to include common/schema.h.
  YBSchema client_projection_;

  Arena arena_;
  AutoReleasePool pool_;

  // Machinery to store and encode raw column range predicates into
  // encoded keys.
  ScanSpec spec_;
  RangePredicateEncoder spec_encoder_;

  // The tablet we're scanning.
  scoped_refptr<internal::RemoteTablet> remote_;

  // Timeout for scanner RPCs.
  MonoDelta timeout_;

  // Number of attempts since the last successful scan.
  int scan_attempts_;

  // The deprecated "NextBatch(vector<YBRowResult>*) API requires some local
  // storage for the actual row data. If that API is used, this member keeps the
  // actual storage for the batch that is returned.
  YBScanBatch batch_for_old_api_;

  // The latest error experienced by this scan that provoked a retry. If the
  // scan times out, this error will be incorporated into the status that is
  // passed back to the client.
  //
  // TODO: This and the overall scan retry logic duplicates much of RpcRetrier.
  Status last_error_;

  DISALLOW_COPY_AND_ASSIGN(Data);
};

class YBScanBatch::Data {
 public:
  Data();
  ~Data();

  CHECKED_STATUS Reset(rpc::RpcController* controller,
               const Schema* projection,
               const YBSchema* client_projection,
               gscoped_ptr<RowwiseRowBlockPB> resp_data);

  int num_rows() const {
    return resp_data_.num_rows();
  }

  YBRowResult row(int idx) {
    DCHECK_GE(idx, 0);
    DCHECK_LT(idx, num_rows());
    int offset = idx * projected_row_size_;
    return YBRowResult(projection_, client_projection_, &direct_data_[offset]);
  }

  void ExtractRows(vector<YBScanBatch::RowPtr>* rows);

  void Clear();

  // Returns the size of a row for the given projection 'proj'.
  static size_t CalculateProjectedRowSize(const Schema& proj);

  // The RPC controller for the RPC which returned this batch.
  // Holding on to the controller ensures we hold on to the indirect data
  // which contains the rows.
  rpc::RpcController controller_;

  // The PB which contains the "direct data" slice.
  RowwiseRowBlockPB resp_data_;

  // Slices into the direct and indirect row data, whose lifetime is ensured
  // by the members above.
  Slice direct_data_, indirect_data_;

  // The projection being scanned.
  const Schema* projection_;
  // The YBSchema version of 'projection_'
  const YBSchema* client_projection_;

  // The number of bytes of direct data for each row.
  size_t projected_row_size_;
};

} // namespace client
} // namespace yb

#endif
