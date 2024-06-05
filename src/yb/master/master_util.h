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

#include <memory>

#include "yb/common/common_fwd.h"
#include "yb/common/common_types.pb.h"
#include "yb/common/entity_ids_types.h"

#include "yb/master/master_client.fwd.h"
#include "yb/master/master_fwd.h"

#include "yb/rpc/rpc_fwd.h"

#include "yb/util/status_fwd.h"
#include "yb/util/monotime.h"

static constexpr const char* kDBTypePrefixUnknown = "unknown";
static constexpr const char* kDBTypePrefixCql = "ycql";
static constexpr const char* kDBTypePrefixYsql = "ysql";
static constexpr const char* kDBTypePrefixRedis = "yedis";

namespace yb {

const char* DatabasePrefix(YQLDatabase db);

// A short version of the given database type, such as "YCQL" or "YSQL". Used
// for human-readable messages.
std::string ShortDatabaseType(YQLDatabase db_type);

namespace consensus {

class RaftPeerPB;

}

namespace master {

// Given a hostport, return the master server information protobuf.
// Does not apply to tablet server.
Status GetMasterEntryForHosts(
    rpc::ProxyCache* proxy_cache,
    const std::vector<HostPort>& hostports,
    MonoDelta timeout,
    ServerEntryPB* e);

const HostPortPB& DesiredHostPort(const TSInfoPB& ts_info, const CloudInfoPB& from);

void TakeRegistration(consensus::RaftPeerPB* source, TSInfoPB* dest);
void CopyRegistration(const consensus::RaftPeerPB& source, TSInfoPB* dest);

void TakeRegistration(ServerRegistrationPB* source, TSInfoPB* dest);
void CopyRegistration(const ServerRegistrationPB& source, TSInfoPB* dest);

bool IsSystemNamespace(const std::string& namespace_name);

YQLDatabase GetDefaultDatabaseType(const std::string& keyspace_name);

template<class PB>
YQLDatabase GetDatabaseType(const PB& ns) {
  return ns.has_database_type() ? ns.database_type() : GetDefaultDatabaseType(ns.name());
}

YQLDatabase GetDatabaseTypeForTable(const TableType table_type);
TableType GetTableTypeForDatabase(const YQLDatabase database_type);

Result<bool> NamespaceMatchesIdentifier(
    const NamespaceId& namespace_id, YQLDatabase db_type, const NamespaceName& namespace_name,
    const NamespaceIdentifierPB& ns_identifier);

Result<bool> TableMatchesIdentifier(const TableId& id,
                                    const SysTablesEntryPB& table,
                                    const TableIdentifierPB& table_identifier);

Status SetupError(MasterErrorPB* error, const Status& s);

// TODO(alex): Merge with stuff in entity_ids?

bool IsBlacklisted(const ServerRegistrationPB& registration, const BlacklistSet& blacklist);

bool IsRunningOn(const ServerRegistrationPB& registration, const HostPortPB& hp);

BlacklistSet ToBlacklistSet(const BlacklistPB& blacklist);

} // namespace master
} // namespace yb
