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

#ifndef YB_YQL_PGGATE_PG_CLIENT_H
#define YB_YQL_PGGATE_PG_CLIENT_H

#include <memory>
#include <string>

#include <boost/preprocessor/seq/for_each.hpp>
#include <boost/version.hpp>

#include "yb/client/client_fwd.h"

#include "yb/common/pg_types.h"

#include "yb/master/master_fwd.h"

#include "yb/rpc/rpc_fwd.h"

#include "yb/tserver/tserver_fwd.h"
#include "yb/tserver/tserver_util_fwd.h"
#include "yb/tserver/pg_client.fwd.h"

#include "yb/util/monotime.h"

#include "yb/yql/pggate/pg_gate_fwd.h"

namespace yb {
namespace pggate {

#define YB_PG_CLIENT_SIMPLE_METHODS \
    (AlterDatabase)(AlterTable)(CreateDatabase)(CreateTable)(CreateTablegroup) \
    (DropDatabase)(DropTablegroup)(TruncateTable)

class PgClient {
 public:
  PgClient();
  ~PgClient();

  CHECKED_STATUS Start(rpc::ProxyCache* proxy_cache,
                       rpc::Scheduler* scheduler,
                       const tserver::TServerSharedObject& tserver_shared_object);
  void Shutdown();

  Result<PgTableDescPtr> OpenTable(const PgObjectId& table_id);

  Result<master::GetNamespaceInfoResponsePB> GetDatabaseInfo(PgOid oid);

  Result<std::pair<PgOid, PgOid>> ReserveOids(PgOid database_oid, PgOid next_oid, uint32_t count);

  Result<bool> IsInitDbDone();

  Result<uint64_t> GetCatalogMasterVersion();

  CHECKED_STATUS CreateSequencesDataTable();

  Result<client::YBTableName> DropTable(
      tserver::PgDropTableRequestPB* req, CoarseTimePoint deadline);

  CHECKED_STATUS BackfillIndex(tserver::PgBackfillIndexRequestPB* req, CoarseTimePoint deadline);

  Result<int32> TabletServerCount(bool primary_only);

  Result<client::TabletServersInfo> ListLiveTabletServers(bool primary_only);

  CHECKED_STATUS ValidatePlacement(const tserver::PgValidatePlacementRequestPB* req);

#define YB_PG_CLIENT_SIMPLE_METHOD_DECLARE(r, data, method) \
  CHECKED_STATUS method(                             \
      tserver::BOOST_PP_CAT(BOOST_PP_CAT(Pg, method), RequestPB)* req, \
      CoarseTimePoint deadline);

  BOOST_PP_SEQ_FOR_EACH(YB_PG_CLIENT_SIMPLE_METHOD_DECLARE, ~, YB_PG_CLIENT_SIMPLE_METHODS);

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace pggate
}  // namespace yb

#endif  // YB_YQL_PGGATE_PG_CLIENT_H
