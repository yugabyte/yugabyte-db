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

#include "yb/common/colocated_util.h"
#include "yb/common/entity_ids.h"
#include "yb/common/pg_types.h"

#include <boost/uuid/nil_generator.hpp>

#include "yb/cdc/cdc_types.h"

#include "yb/gutil/strings/escaping.h"
#include "yb/util/cast.h"
#include "yb/util/result.h"
#include "yb/util/strongly_typed_bool.h"

using std::string;

using boost::uuids::uuid;

namespace yb {

namespace {

// Dev Note: When the major catalog version changes, meaning, for the YugabyteDB version that uses
// PG17+ for the YSQL layer, update kPgPreviousUuidVersion to kPgCurrentUuidVersion, and increase
// kPgCurrentUuidVersion by 1.
constexpr uint8_t kPgPreviousUuidVersion = 0;  // PG11
constexpr uint8_t kPgCurrentUuidVersion = 1;   // PG15

}  // namespace

static constexpr int kUuidVersion = 3; // Repurpose old name-based UUID v3 to embed Postgres oids.

const uint32_t kPgProcTableOid = 1255;  // Hardcoded for pg_proc. (in pg_proc.h)

// This should match the value for pg_tablespace hardcoded in pg_tablespace.h
const uint32_t kPgTablespaceTableOid = 1213;

// Static initialization is OK because this won't be used in another static initialization.
const TableId kPgProcTableId = GetPgsqlTableId(kTemplate1Oid, kPgProcTableOid);
const TableId kPgYbCatalogVersionTableId =
    GetPgsqlTableId(kTemplate1Oid, kPgYbCatalogVersionTableOid);
const TableId kPgYbCatalogVersionTableIdPriorVersion =
    GetPriorVersionYsqlCatalogTableId(kTemplate1Oid, kPgYbCatalogVersionTableOid);
const TableId kPgTablespaceTableId =
    GetPgsqlTableId(kTemplate1Oid, kPgTablespaceTableOid);
const TableId kPgSequencesDataTableId =
    GetPgsqlTableId(kPgSequencesDataDatabaseOid, kPgSequencesDataTableOid);
const string kPgSequencesDataNamespaceId =
  GetPgsqlNamespaceId(kPgSequencesDataDatabaseOid);



//-------------------------------------------------------------------------------------------------

namespace {

// Layout of Postgres database and table 4-byte oids in a YugaByte 16-byte table UUID:
//
// +-----------------------------------------------------------------------------------------------+
// |  0  |  1  |  2  |  3  |  4  |  5  |  6  |  7  |  8  |  9  |  10 |  11 |  12 |  13 |  14 |  15 |
// +-----------------------------------------------------------------------------------------------+
// |        database       |           | vsn |     | var | pgv |           |        table          |
// |          oid          |           |     |     |     |     |           |         oid           |
// +-----------------------------------------------------------------------------------------------+
//
// vsn = UUID version
// var = UUID variant
//
// pgv = for PG catalog tables only, the PG version for the table.
// 0 = PG11
// 1 = PG15
// 2 = PG17 (or whatever major PG version YB integrates next)
// ...
// Must be set to 0 for all user tables.

void UuidSetDatabaseId(const uint32_t database_oid, uuid* id) {
  id->data[0] = database_oid >> 24 & 0xFF;
  id->data[1] = database_oid >> 16 & 0xFF;
  id->data[2] = database_oid >> 8  & 0xFF;
  id->data[3] = database_oid & 0xFF;
}

void UuidSetTableIds(const uint32_t table_oid, uuid* id) {
  id->data[12] = table_oid >> 24 & 0xFF;
  id->data[13] = table_oid >> 16 & 0xFF;
  id->data[14] = table_oid >> 8  & 0xFF;
  id->data[15] = table_oid & 0xFF;
}

inline void UuidSetPgVersion(uuid& id, bool is_current_version) {
  id.data[9] = is_current_version ? kPgCurrentUuidVersion : kPgPreviousUuidVersion;
}

inline bool IsCurrentPgVersion(const TableId& table_id) {
  const auto binary_id = a2b_hex(table_id);
  return binary_id[9] == kPgCurrentUuidVersion;
}

std::string UuidToString(uuid* id) {
  // Set variant that is stored in octet 7, which is index 8, since indexes count backwards.
  // Variant must be 0b10xxxxxx for RFC 4122 UUID variant 1.
  id->data[8] &= 0xBF;
  id->data[8] |= 0x80;

  // Set version that is stored in octet 9 which is index 6, since indexes count backwards.
  id->data[6] &= 0x0F;
  id->data[6] |= (kUuidVersion << 4);

  return b2a_hex(to_char_ptr(id->data), sizeof(id->data));
}

TableId GetPgsqlTableIdInternal(
    const uint32_t database_oid, const uint32_t table_oid, bool is_current_version) {
  uuid id = boost::uuids::nil_uuid();
  UuidSetDatabaseId(database_oid, &id);

  // For catalog tables, we need to set the correct version in id.data[9], which is "pgv" in the
  // above diagram. Note that normal object IDs are versionless, always id.data[9] == 0.
  if (table_oid < kPgFirstNormalObjectId) {
    UuidSetPgVersion(id, is_current_version);
  } else {
    LOG_IF(DFATAL, !is_current_version) << "User table IDs do not have prior versions.";
  }

  UuidSetTableIds(table_oid, &id);
  return UuidToString(&id);
}

bool IsYsqlCatalogTable(const TableId& table_id) {
  if (!IsPgsqlId(table_id)) {
    return false;
  }
  Result<uint32_t> oid_res = GetPgsqlTableOid(table_id);
  if (!oid_res.ok()) {
    YB_LOG_EVERY_N_SECS(WARNING, 5)
        << "Invalid PostgreSQL table id " << table_id << ": " << oid_res.status();
    return false;
  }

  return *oid_res < kPgFirstNormalObjectId;
}

} // namespace

NamespaceId GetPgsqlNamespaceId(const uint32_t database_oid) {
  uuid id = boost::uuids::nil_uuid();
  UuidSetDatabaseId(database_oid, &id);
  return UuidToString(&id);
}

TableId GetPgsqlTableId(const uint32_t database_oid, const uint32_t table_oid) {
  return GetPgsqlTableIdInternal(database_oid, table_oid, /*is_current_version=*/true);
}

TablegroupId GetPgsqlTablegroupId(const uint32_t database_oid, const uint32_t tablegroup_oid) {
  return GetPgsqlTableId(database_oid, tablegroup_oid);
}

TablespaceId GetPgsqlTablespaceId(const uint32_t tablespace_oid) {
  uuid id = boost::uuids::nil_uuid();
  // Tablespace is an entity across databases so this is better than UuidSetTableIds.
  UuidSetDatabaseId(tablespace_oid, &id);
  return UuidToString(&id);
}

bool IsPgsqlId(const string& id) {
  if (id.size() != 32) return false; // Ignore non-UUID id like "sys.catalog.uuid"
  try {
    size_t pos = 0;
#ifndef NDEBUG
    const int variant = std::stoi(id.substr(8 * 2, 2), &pos, 16);
    DCHECK((pos == 2) && (variant & 0xC0) == 0x80) << "Invalid Postgres id " << id;
#endif

    const int version = std::stoi(id.substr(6 * 2, 2), &pos, 16);
    if ((pos == 2) && (version & 0xF0) >> 4 == kUuidVersion) return true;

  } catch(const std::invalid_argument&) {
  } catch(const std::out_of_range&) {
  }

  return false;
}

Result<uint32_t> GetPgsqlOid(const std::string& str, size_t offset, const char* name) {
  SCHECK(IsPgsqlId(str), InvalidArgument, Format("Not a YSQL ID string: $0", str));
  try {
    size_t pos = 0;
    const uint32_t oid = static_cast<uint32_t>(
        stoul(str.substr(offset, sizeof(uint32_t) * 2), &pos, 16));
    if (pos == sizeof(uint32_t) * 2) {
      return oid;
    }
  } catch(const std::invalid_argument&) {
  } catch(const std::out_of_range&) {
  }

  return STATUS_FORMAT(InvalidArgument, "Invalid PostgreSQL $0: $1", name, str);
}

Result<uint32_t> GetPgsqlDatabaseOid(const NamespaceId& namespace_id) {
  return GetPgsqlOid(namespace_id, 0, "namespace id");
}

Result<uint32_t> GetPgsqlTableOid(const TableId& table_id) {
  return GetPgsqlOid(table_id, 12 * 2, "table id");
}

Result<uint32_t> GetPgsqlTablegroupOid(const TablegroupId& tablegroup_id) {
  return GetPgsqlOid(tablegroup_id, 12 * 2, "tablegroup id");
}

Result<uint32_t> GetPgsqlTablegroupOidByTableId(const TableId& table_id) {
  if (table_id.size() < 32) {
    return STATUS(InvalidArgument, "Invalid PostgreSQL table id for tablegroup", table_id);
  }
  TablegroupId tablegroup_id = table_id.substr(0, 32);

  return GetPgsqlTablegroupOid(tablegroup_id);
}

Result<uint32_t> GetPgsqlDatabaseOidByTableId(const TableId& table_id) {
  return GetPgsqlOid(table_id, 0, "table id");
}

Result<uint32_t> GetPgsqlDatabaseOidByTablegroupId(const TablegroupId& tablegroup_id) {
  return GetPgsqlOid(tablegroup_id, 0, "tablegroup id");
}

Result<uint32_t> GetPgsqlTablespaceOid(const TablespaceId& tablespace_id) {
  return GetPgsqlOid(tablespace_id, 0, "tablespace id");
}

TableId GetPriorVersionYsqlCatalogTableId(const uint32_t database_oid, const uint32_t table_oid) {
  return GetPgsqlTableIdInternal(database_oid, table_oid, /*is_current_version=*/false);
}

bool IsPriorVersionYsqlCatalogTable(const TableId& table_id) {
  if (!IsYsqlCatalogTable(table_id)) {
    return false;
  }

  return !IsCurrentPgVersion(table_id);
}

bool IsCurrentVersionYsqlCatalogTable(const TableId& table_id) {
  if (!IsYsqlCatalogTable(table_id)) {
    return false;
  }

  return IsCurrentPgVersion(table_id);
}

Result<TableId> GetRestoreTargetTablegroupId(
    const NamespaceId& restore_target_namespace_id, const TableId& backup_source_tablegroup_id) {
  // Since we preserve tablegroup oid in ysql_dump, then generate the target_tablegroup_id using
  // restore_target_namespace_id and backup_source_tablegroup_id.
  PgOid target_database_oid = VERIFY_RESULT(GetPgsqlDatabaseOid(restore_target_namespace_id));
  PgOid tablegroup_oid = VERIFY_RESULT(
      GetPgsqlTablegroupOid(GetTablegroupIdFromParentTableId(backup_source_tablegroup_id)));
  auto target_tablegroup_id = GetPgsqlTablegroupId(target_database_oid, tablegroup_oid);
  if (IsColocatedDbTablegroupParentTableId(backup_source_tablegroup_id)) {
    // This tablegroup parent table is in a colocated database, and has string
    // 'colocation' in its id.
    return GetColocationParentTableId(target_tablegroup_id);
  } else {
    return GetTablegroupParentTableId(target_tablegroup_id);
  }
  return target_tablegroup_id;
}

Result<TableId> GetRestoreTargetTableIdUsingRelfilenode(
    const NamespaceId& restore_target_namespace_id, const TableId& backup_source_table_id) {
  // new_table_id at restore side (if existed) and old_table_id (from backup side) have the same
  // relfilenode. This is the last 4 bytes in the DocDB table UUID. Construct the new table UUID.
  PgOid relfilenode = VERIFY_RESULT(GetPgsqlTableOid(backup_source_table_id));
  PgOid restore_target_db_oid = VERIFY_RESULT(GetPgsqlDatabaseOid(restore_target_namespace_id));
  return GetPgsqlTableId(restore_target_db_oid, relfilenode);
}

namespace xrepl {
YB_STRONGLY_TYPED_HEX_UUID_IMPL(StreamId);
}

}  // namespace yb
