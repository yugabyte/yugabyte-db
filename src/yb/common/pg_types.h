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

#include <boost/functional/hash/hash.hpp>

#include "yb/common/entity_ids.h"

namespace yb {

class Slice;

// Postgres object identifier (OID).
typedef uint32_t PgOid;
static constexpr PgOid kPgInvalidOid = 0;
static constexpr PgOid kPgByteArrayOid = 17;

// A struct to identify a Postgres object by oid and the database oid it belongs to.
struct PgObjectId {
  PgOid database_oid = kPgInvalidOid;
  PgOid object_oid = kPgInvalidOid;

  PgObjectId(PgOid db_oid, PgOid obj_oid)
      : database_oid(db_oid), object_oid(obj_oid) {}

  PgObjectId()
      : database_oid(kPgInvalidOid), object_oid(kPgInvalidOid) {}

  explicit PgObjectId(const TableId& table_id);
  explicit PgObjectId(const Slice& table_id);

  bool IsValid() const {
    return database_oid != kPgInvalidOid && object_oid != kPgInvalidOid;
  }

  TableId GetYbTableId() const {
    return GetPgsqlTableId(database_oid, object_oid);
  }

  TablegroupId GetYbTablegroupId() const {
    return GetPgsqlTablegroupId(database_oid, object_oid);
  }

  TablespaceId GetYbTablespaceId() const {
    return GetPgsqlTablespaceId(object_oid);
  }

  std::string ToString() const;

  bool operator== (const PgObjectId& other) const {
    return database_oid == other.database_oid && object_oid == other.object_oid;
  }

  friend std::size_t hash_value(const PgObjectId& id) {
    std::size_t value = 0;
    boost::hash_combine(value, id.database_oid);
    boost::hash_combine(value, id.object_oid);
    return value;
  }

  template <class PB>
  void ToPB(PB* pb) const {
    pb->set_database_oid(database_oid);
    pb->set_object_oid(object_oid);
  }

  template <class PB>
  static PgObjectId FromPB(const PB& pb) {
    return PgObjectId(pb.database_oid(), pb.object_oid());
  }

  template <class PB>
  static TableId GetYbTableIdFromPB(const PB& pb) {
    return FromPB(pb).GetYbTableId();
  }
};

typedef boost::hash<PgObjectId> PgObjectIdHash;

inline std::ostream& operator<<(std::ostream& out, const PgObjectId& id) {
  return out << id.ToString();
}

}  // namespace yb
