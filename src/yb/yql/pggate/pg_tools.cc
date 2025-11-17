//--------------------------------------------------------------------------------------------------
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
//--------------------------------------------------------------------------------------------------

#include "yb/yql/pggate/pg_tools.h"

#include <algorithm>
#include <cstring>

#include <boost/container/small_vector.hpp>
#include <boost/functional/hash/hash.hpp>

#include "yb/common/pg_system_attr.h"

#include "yb/util/memory/arena.h"
#include "yb/util/result.h"

#include "yb/yql/pggate/pg_doc_op.h"
#include "yb/yql/pggate/pg_session.h"
#include "yb/yql/pggate/pg_table.h"
#include "yb/yql/pggate/pg_type.h"

DECLARE_uint32(TEST_yb_ash_sleep_at_wait_state_ms);
DECLARE_uint32(TEST_yb_ash_wait_code_to_sleep_at);
DECLARE_string(TEST_yb_test_wait_event_aux_to_sleep_at_csv);

namespace yb::pggate {
namespace {

const std::locale& CSVLocale() {
  using ctype = std::ctype<char>;
  static const auto mask = [] {
    const auto* classic_table = ctype::classic_table();
    std::vector<ctype::mask> v(classic_table, classic_table + ctype::table_size);
    v[','] |= ctype::space;
    v[' '] &= ~ctype::space;
    return v;
  }();
  static std::locale locale({}, new ctype(mask.data()));
  return locale;
}

std::unordered_set<ash::PggateRPC> ParseRPCsFromCSV(const std::string& csv) {
  std::unordered_set<ash::PggateRPC> rpcs;
  std::istringstream csv_stream(csv);
  csv_stream.imbue(CSVLocale());
  ash::PggateRPC rpc;

  while (csv_stream >> rpc) {
    rpcs.insert(rpc);
  }

  return rpcs;
}

bool IsSleepRequired(ash::PggateRPC rpc) {
  static std::unordered_set<ash::PggateRPC> rpcs =
      ParseRPCsFromCSV(FLAGS_TEST_yb_test_wait_event_aux_to_sleep_at_csv);
  return rpcs.contains(rpc);
}

inline bool MaybeSleepForTests(ash::WaitStateCode wait_event, ash::PggateRPC pggate_rpc) {
  return FLAGS_TEST_yb_ash_sleep_at_wait_state_ms > 0 && (
      FLAGS_TEST_yb_ash_wait_code_to_sleep_at == std::to_underlying(wait_event) ||
      IsSleepRequired(pggate_rpc));
}

bool IsEqual(const YbcPgTableLocalityInfo& lhs, const YbcPgTableLocalityInfo& rhs) {
  return lhs.is_region_local == rhs.is_region_local && lhs.tablespace_oid == rhs.tablespace_oid;
}

bool IsEmpty(const YbcPgTableLocalityInfo& info) {
  return IsEqual(info, {});
}

} // namespace

RowMarkType GetRowMarkType(const YbcPgExecParameters* exec_params) {
  return exec_params && exec_params->rowmark > -1
      ? static_cast<RowMarkType>(exec_params->rowmark)
      : RowMarkType::ROW_MARK_ABSENT;
}

PgWaitEventWatcher::PgWaitEventWatcher(
    Starter starter, ash::WaitStateCode wait_event, ash::PggateRPC pggate_rpc)
    : starter_(starter),
      prev_wait_event_(starter_({std::to_underlying(wait_event), std::to_underlying(pggate_rpc)})) {
  if (PREDICT_FALSE(MaybeSleepForTests(wait_event, pggate_rpc))) {
    SleepFor(MonoDelta::FromMilliseconds(FLAGS_TEST_yb_ash_sleep_at_wait_state_ms));
  }
}

PgWaitEventWatcher::~PgWaitEventWatcher() {
  starter_(prev_wait_event_);
}

MemoryOptimizedTableYbctid::MemoryOptimizedTableYbctid(PgOid table_id_, std::string_view ybctid_)
    : table_id(table_id_),
      ybctid_size(static_cast<uint32_t>(ybctid_.size())),
      ybctid_data(new char[ybctid_size]) {
    std::memcpy(ybctid_data.get(), ybctid_.data(), ybctid_size);
}

MemoryOptimizedTableYbctid::operator LightweightTableYbctid() const {
  return LightweightTableYbctid(table_id, std::string_view(ybctid_data.get(), ybctid_size));
}

size_t TableYbctidHasher::operator()(const LightweightTableYbctid& value) const {
  size_t hash = 0;
  boost::hash_combine(hash, value.table_id);
  boost::hash_range(hash, value.ybctid.begin(), value.ybctid.end());
  return hash;
}

Slice YbctidAsSlice(const PgTypeInfo& pg_types, uint64_t ybctid) {
  char* value = nullptr;
  int64_t bytes = 0;
  pg_types.GetYbctid().datum_to_yb(ybctid, &value, &bytes);
  return Slice(value, bytes);
}

std::string ToString(const YbcObjectLockId& lock_id) {
  return Format(
      "object { db_oid: $0, table_oid: $1, object_id: $2, object_sub_oid: $3 }",
      lock_id.db_oid, lock_id.relation_oid, lock_id.object_oid, lock_id.object_sub_oid);
}

std::string ToString(const YbcAdvisoryLockId& lock_id) {
  return Format(
      "advisory lock { db_oid: $0, classid: $1, object_oid: $2, object_sub_oid: $3 } ",
      lock_id.database_id, lock_id.classid, lock_id.objid, lock_id.objsubid);
}

TablespaceCache::TablespaceCache(size_t capacity) : impl_(capacity) {}

std::optional<PgTablespaceOid> TablespaceCache::Get(PgObjectId table_oid) {
  const auto i = impl_.find(table_oid);
  return i != impl_.end() ? std::optional(i->tablespace_oid) : std::nullopt;
}

void TablespaceCache::Put(PgObjectId table_oid, PgTablespaceOid tablespace_oid) {
  auto i = impl_.insert(Info{.key = table_oid, .tablespace_oid = tablespace_oid});
  i->tablespace_oid = tablespace_oid;
}

void TablespaceCache::Clear() {
  impl_.clear();
}

void TableLocalityMap::Add(PgOid table_id, const YbcPgTableLocalityInfo& info) {
  if (IsEmpty(info)) {
    DCHECK(!map_.contains(table_id));
    return;
  }
  [[maybe_unused]] const auto ipair = map_.emplace(table_id, info);
  DCHECK(ipair.second || IsEqual(ipair.first->second, info));
}

const YbcPgTableLocalityInfo& TableLocalityMap::Get(PgOid table_id) const {
  static const YbcPgTableLocalityInfo kEmpty{};
  const auto i = map_.find(table_id);
  return i == map_.end() ? kEmpty : i->second;
}

void TableLocalityMap::Clear() {
  map_.clear();
}

} // namespace yb::pggate
