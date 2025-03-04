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

#include "yb/ash/wait_state.h"

#include "yb/tserver/pg_client.pb.h"

namespace yb::tserver {

// Each synchronous RequestPB should have a `AshMetadataPB ash_metadata`
// field, there should be a new enum value added in ash::PggateRPC in
// wait_state.h with the same name as the RPC along with a 'k' prefix
// and the DoSyncRPC method should be used in PgClient::Impl to
// automatically fill up the ash_metadata field. For example,
//
// in pg_client.proto
//
// message PgGetTablePartitionListRequestPB {
//   ...
//   AshMetadataPB ash_metadata = 2;
// }
//
// in wait_state.h
//
// YB_DEFINE_TYPED_ENUM(PggateRPC, uint16_t,
//   ...
//   (kGetTablePartitionList)
// );
//
// in pg_client.cc
//
// Result<...> GetTablePartitionList(...) {
//   ...
//   RETURN_NOT_OK(DoSyncRPC(&tserver::PgClientServiceProxy::GetTablePartitionList,
//       req, resp, ash::PggateRPC::kGetTablePartitionList));
//   ...
// }
template <class RequestPB>
void TryUpdateAshWaitState(const RequestPB& req) {
  if (req.has_ash_metadata()) {
    ash::WaitStateInfo::UpdateMetadataFromPB(req.ash_metadata());
  }
}

// Overloads for RequestPBs which intentionally doesn't have the ash_metadata
// field, either because they are deprecated, or they are async RPCs, or
// they are called before ASH is able to sample them as of 04-03-2025
//
// NOTE: New sync RequestPBs should have ASH metadata along with it, and it
// shouldn't be overloaded here.
inline void TryUpdateAshWaitState(const PgHeartbeatRequestPB&) {}
inline void TryUpdateAshWaitState(const PgActiveSessionHistoryRequestPB&) {}
inline void TryUpdateAshWaitState(const PgFetchDataRequestPB&) {}
inline void TryUpdateAshWaitState(const PgGetCatalogMasterVersionRequestPB&) {}
inline void TryUpdateAshWaitState(const PgGetDatabaseInfoRequestPB&) {}
inline void TryUpdateAshWaitState(const PgIsInitDbDoneRequestPB&) {}
inline void TryUpdateAshWaitState(const PgCreateSequencesDataTableRequestPB&) {}

} // namespace yb::tserver
