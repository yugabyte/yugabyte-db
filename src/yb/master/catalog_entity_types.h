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

#include <boost/preprocessor/list/for_each.hpp>
#include <boost/preprocessor/seq/for_each.hpp>
#include <boost/preprocessor/seq/to_list.hpp>

#include "yb/master/master_types.pb.h"

#pragma once

namespace yb::master {

class SysRowEntry;

// -----------------------------------------------------------------------------
// CATALOG_ENTITY_TYPE_MAP
// -----------------------------------------------------------------------------
//
// SysRowEntryType to SysCatalog entity PB type mapping
// Add new entries to the end of the list.

#define CATALOG_ENTITY_TYPE_MAP \
  ((TABLE, SysTablesEntryPB)) \
  ((TABLET, SysTabletsEntryPB)) \
  ((CLUSTER_CONFIG, SysClusterConfigEntryPB)) \
  ((NAMESPACE, SysNamespaceEntryPB)) \
  ((UDTYPE, SysUDTypeEntryPB)) \
  ((ROLE, SysRoleEntryPB)) \
  ((SNAPSHOT, SysSnapshotEntryPB)) \
  ((REDIS_CONFIG, SysRedisConfigEntryPB)) \
  ((SYS_CONFIG, SysConfigEntryPB)) \
  ((CDC_STREAM, SysCDCStreamEntryPB)) \
  ((UNIVERSE_REPLICATION, SysUniverseReplicationEntryPB)) \
  ((SNAPSHOT_SCHEDULE, SnapshotScheduleOptionsPB)) \
  ((DDL_LOG_ENTRY, DdlLogEntryPB)) \
  ((SNAPSHOT_RESTORATION, SysRestorationEntryPB)) \
  ((XCLUSTER_SAFE_TIME, XClusterSafeTimePB)) \
  ((XCLUSTER_CONFIG, SysXClusterConfigEntryPB)) \
  ((UNIVERSE_REPLICATION_BOOTSTRAP, SysUniverseReplicationBootstrapEntryPB)) \
  ((XCLUSTER_OUTBOUND_REPLICATION_GROUP, SysXClusterOutboundReplicationGroupEntryPB)) \
  ((CLONE_STATE, SysCloneStatePB)) \
  ((TSERVER_REGISTRATION, SysTServerEntryPB))

// We should have an entry for each SysRowEntryType in the map except for UNKNOWN.
static_assert(
    BOOST_PP_SEQ_SIZE(CATALOG_ENTITY_TYPE_MAP) == (yb::master::SysRowEntryType_ARRAYSIZE - 1),
    "CATALOG_ENTITY_TYPE_MAP must contain all SysRowEntryType entries");

// -----------------------------------------------------------------------------
// GetCatalogEntityType
// -----------------------------------------------------------------------------
//
// Template types to retrieve the SysRowEntryType from a catalog entity PB.
// Ex:
//  GetCatalogEntityType<SysTablesEntryPB>::value will return yb::master::SysRowEntryType::TABLE

template <class PB>
struct GetCatalogEntityType;

#define DO_CATALOG_ENTITY_TYPE_OF(entry_type, pb_type) \
  class pb_type; \
  template <> \
  struct GetCatalogEntityType<pb_type> \
      : public std::integral_constant< \
            yb::master::SysRowEntryType, yb::master::SysRowEntryType::entry_type> {};

#define CATALOG_ENTITY_TYPE_OF(r, data, elem) DO_CATALOG_ENTITY_TYPE_OF elem

// Generate type specializations for all the catalog entity types.
// Ex:
//    GetCatalogEntityType<SysTablesEntryPB> :
//      public std::integral_constant<yb::master::SysRowEntryType,
//        yb::master::SysRowEntryType::TABLE> {};
BOOST_PP_SEQ_FOR_EACH(CATALOG_ENTITY_TYPE_OF, ~, CATALOG_ENTITY_TYPE_MAP);

}  // namespace yb::master
