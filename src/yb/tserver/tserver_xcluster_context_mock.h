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

#pragma once

#include <gmock/gmock.h>

#include "yb/common/hybrid_time.h"
#include "yb/common/pg_types.h"

#include "yb/tserver/tserver_xcluster_context_if.h"

#include "yb/util/result.h"

namespace yb {

// This is needed for the mock of GetSafeTime.
class HybridTime;
std::ostream& operator<<(std::ostream& os, const Result<std::optional<HybridTime>>& res);

namespace tserver {

class MockTserverXClusterContext : public TserverXClusterContextIf {
 public:
  MOCK_METHOD(
      (Result<std::optional<HybridTime>>), GetSafeTime, (NamespaceIdView namespace_id),
      (const, override));

  MOCK_METHOD(
      XClusterNamespaceInfoPB_XClusterRole, GetXClusterRole, (NamespaceIdView namespace_id),
      (const, override));

  MOCK_METHOD(bool, IsReadOnlyMode, (NamespaceIdView namespace_id), (const, override));
  MOCK_METHOD(
      bool, IsTargetAndInAutomaticMode, (const NamespaceId& namespace_id), (const, override));

  MOCK_METHOD(bool, SafeTimeComputationRequired, (), (const, override));
  MOCK_METHOD(
      bool, SafeTimeComputationRequired, (const NamespaceId& namespace_id), (const, override));

  MOCK_METHOD(
      void, UpdateTargetNamespacesInAutomaticModeSet,
      (const std::unordered_set<NamespaceId>& target_namespaces_in_automatic_mode), (override));

  MOCK_METHOD(
      Status, SetSourceTableInfoMappingForCreateTable,
      (const YsqlFullTableName& table_name, const PgObjectId& producer_table_id,
       ColocationId colocation_id, const HybridTime& backfill_time_opt),
      (override));
  MOCK_METHOD(
      void, ClearSourceTableInfoMappingForCreateTable, (const YsqlFullTableName& table_name),
      (override));

  MOCK_METHOD(
      void, PrepareCreateTableHelper, (const PgCreateTableRequestPB& req, PgCreateTable& helper),
      (const, override));
};

}  // namespace tserver
}  // namespace yb
