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

#include "yb/cdc/cdc_service.pb.h"
#include "yb/cdc/cdc_service.proxy.h"

#include "yb/cdc/cdc_service.h"

#include "yb/integration-tests/mini_cluster.h"

namespace yb {
namespace xrepl {
class CDCSDKTabletMetrics;
class XClusterTabletMetrics;
}  // namespace xrepl
namespace cdc {

Result<QLValuePB> ExtractKey(
    const Schema& schema, const cdc::KeyValuePairPB& key, std::string expected_col_name,
    size_t col_id = 0, bool range_col = false);

void AssertIntKey(
    const Schema& schema, const google::protobuf::RepeatedPtrField<cdc::KeyValuePairPB>& key,
    int32_t value);

Result<xrepl::StreamId> CreateXClusterStream(client::YBClient& client, const TableId& table_id);

// For any tablet that belongs to a table whose name starts with 'table_name_start', this method
// will verify that its WAL retention time matches the provided time.
// It will also verify that at least one WAL retention time was checked.
void VerifyWalRetentionTime(yb::MiniCluster* cluster,
                            const std::string& table_name_start,
                            uint32_t expected_wal_retention_secs);

Status CorrectlyPollingAllTablets(
    MiniCluster* cluster, size_t num_producer_tablets, MonoDelta timeout);

Result<std::shared_ptr<xrepl::XClusterTabletMetrics>> GetXClusterTabletMetrics(
    cdc::CDCServiceImpl& cdc_service, const TabletId& tablet_id, const xrepl::StreamId stream_id,
    cdc::CreateMetricsEntityIfNotFound create = cdc::CreateMetricsEntityIfNotFound::kTrue);

Result<std::shared_ptr<xrepl::CDCSDKTabletMetrics>> GetCDCSDKTabletMetrics(
    cdc::CDCServiceImpl& cdc_service, const TabletId& tablet_id, const xrepl::StreamId stream_id,
    cdc::CreateMetricsEntityIfNotFound create = cdc::CreateMetricsEntityIfNotFound::kTrue);
} // namespace cdc
} // namespace yb
