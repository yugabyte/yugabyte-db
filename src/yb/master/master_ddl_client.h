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

#include "yb/common/entity_ids_types.h"

#include "yb/master/master_ddl.proxy.h"

#include "yb/util/monotime.h"

namespace yb::master {
class MasterDDLClient {
 public:
  explicit MasterDDLClient(MasterDdlProxy&& proxy) noexcept;

  Result<TableId> CreateTable(const CreateTableRequestPB& request);

  Status WaitForCreateTableDone(const TableId& id, MonoDelta timeout);

  Result<NamespaceId> CreateNamespace(
      const NamespaceName& namespace_name, YQLDatabase namespace_type);

  Status WaitForCreateNamespaceDone(const NamespaceId& id, MonoDelta timeout);

  Result<NamespaceId> CreateNamespaceAndWait(
      const NamespaceName& namespace_name, YQLDatabase namespace_type, MonoDelta timeout);

  Result<RefreshYsqlLeaseInfoPB> RefreshYsqlLease(
      const std::string& permanent_uuid, int64_t instance_seqno, uint64_t time_ms,
      std::optional<uint64_t> current_lease_epoch);

  Status RelinquishYsqlLease(const std::string& permanent_uuid, int64_t instance_seqno);

  Status DeleteTable(const TableId& id, MonoDelta timeout);

  Result<ListTablesResponsePB> ListTables();

 private:
  MasterDdlProxy proxy_;
};

}  // namespace yb::master
