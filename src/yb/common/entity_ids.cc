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

#include <boost/uuid/nil_generator.hpp>

#include "yb/cdc/cdc_types.h"

#include "yb/common/entity_ids.h"

#include "yb/gutil/strings/escaping.h"
#include "yb/util/cast.h"
#include "yb/util/result.h"
#include "yb/util/strongly_typed_bool.h"

using std::string;

using boost::uuids::uuid;

namespace yb {

namespace {

TableId GetPgsqlTableIdPg11(const uint32_t database_oid, const uint32_t table_oid);

}  // namespace

static constexpr int kUuidVersion = 3; // Repurpose old name-based UUID v3 to embed Postgres oids.

const uint32_t kPgProcTableOid = 1255;  // Hardcoded for pg_proc. (in pg_proc.h)

// This should match the value for pg_yb_catalog_version hardcoded in pg_yb_catalog_version.h.
const uint32_t kPgYbCatalogVersionTableOid = 8010;

// This should match the value for pg_tablespace hardcoded in pg_tablespace.h
const uint32_t kPgTablespaceTableOid = 1213;

// Static initialization is OK because this won't be used in another static initialization.
const TableId kPgProcTableId = GetPgsqlTableId(kTemplate1Oid, kPgProcTableOid);
const TableId kPgYbCatalogVersionTableId =
    GetPgsqlTableId(kTemplate1Oid, kPgYbCatalogVersionTableOid);
const TableId kPgYbCatalogVersionTableIdPg11 =
    GetPgsqlTableIdPg11(kTemplate1Oid, kPgYbCatalogVersionTableOid);
const TableId kPgTablespaceTableId =
    GetPgsqlTableId(kTemplate1Oid, kPgTablespaceTableOid);
const TableId kPgSequencesDataTableId =
    GetPgsqlTableId(kPgSequencesDataDatabaseOid, kPgSequencesDataTableOid);
const string kPgSequencesDataNamespaceId =
  GetPgsqlNamespaceId(kPgSequencesDataDatabaseOid);



//-------------------------------------------------------------------------------------------------

namespace {

YB_STRONGLY_TYPED_BOOL(IsPg15);

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
    const uint32_t database_oid, const uint32_t table_oid, IsPg15 is_pg15) {
  uuid id = boost::uuids::nil_uuid();
  UuidSetDatabaseId(database_oid, &id);

  if (table_oid < kPgFirstNormalObjectId && is_pg15) {
    // "pgv" in the above diagram
    id.data[9] = 1;
  }

  UuidSetTableIds(table_oid, &id);
  return UuidToString(&id);
}

TableId GetPgsqlTableIdPg11(const uint32_t database_oid, const uint32_t table_oid) {
  return GetPgsqlTableIdInternal(database_oid, table_oid, IsPg15::kFalse);
}

} // namespace

NamespaceId GetPgsqlNamespaceId(const uint32_t database_oid) {
  uuid id = boost::uuids::nil_uuid();
  UuidSetDatabaseId(database_oid, &id);
  return UuidToString(&id);
}

TableId GetPgsqlTableId(const uint32_t database_oid, const uint32_t table_oid) {
  return GetPgsqlTableIdInternal(database_oid, table_oid, IsPg15::kTrue);
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

bool IsPg15CatalogId(const std::string& hex_id) {
  if (!IsPgsqlId(hex_id)) {
    return false;
  }
  std::string id = a2b_hex(hex_id);
  return id[9] == 1;
}

namespace xrepl {
YB_STRONGLY_TYPED_HEX_UUID_IMPL(StreamId);
}

}  // namespace yb
