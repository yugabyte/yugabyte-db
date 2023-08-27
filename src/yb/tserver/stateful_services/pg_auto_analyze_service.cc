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

#include "yb/tserver/stateful_services/pg_auto_analyze_service.h"

#include "yb/util/logging.h"

namespace yb {

namespace stateful_service {
PgAutoAnalyzeService::PgAutoAnalyzeService(
    const scoped_refptr<MetricEntity>& metric_entity,
    const std::shared_future<client::YBClient*>& client_future)
    : StatefulRpcServiceBase(StatefulServiceKind::PG_AUTO_ANALYZE, metric_entity, client_future) {}

void PgAutoAnalyzeService::Activate() { LOG(INFO) << "PgAutoAnalyzeService activated"; }

void PgAutoAnalyzeService::Deactivate() { LOG(INFO) << "PgAutoAnalyzeService de-activated"; }

Result<bool> PgAutoAnalyzeService::RunPeriodicTask() {
  VLOG(5) << "PgAutoAnalyzeService Running";
  // TODO: Trigger ANALYZE for tables if their mutation count has reached specific thresholds.
  return true;
}

Status PgAutoAnalyzeService::IncreaseMutationCountersImpl(
    const IncreaseMutationCountersRequestPB& req, IncreaseMutationCountersResponsePB* resp) {
  VLOG_WITH_FUNC(5) << "req=" << req.ShortDebugString();

  // TODO: Update the underlying YCQL table that tracks cluster-wide mutations since the last
  // ANALYZE for all Pg tables.

  return Status::OK();
}

}  // namespace stateful_service
}  // namespace yb
