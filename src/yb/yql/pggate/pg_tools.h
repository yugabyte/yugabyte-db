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
//
// Structure definitions for a Postgres table descriptor.
//--------------------------------------------------------------------------------------------------

#pragma once

#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <boost/multi_index/member.hpp>

#include "yb/ash/wait_state.h"

#include "yb/common/pg_types.h"
#include "yb/common/transaction.pb.h"

#include "yb/gutil/macros.h"
#include "yb/gutil/ref_counted.h"

#include "yb/util/lru_cache.h"
#include "yb/util/lw_function.h"
#include "yb/util/slice.h"
#include "yb/util/status.h"

#include "yb/yql/pggate/ybc_pg_typedefs.h"

namespace yb::pggate {

class PgSession;
class PgTypeInfo;

RowMarkType GetRowMarkType(const YbcPgExecParameters* exec_params);

struct Bound {
  uint16_t value;
  bool is_inclusive;
};

// A helper to set the specified wait_event_info in the specified
// MyProc and revert to the previous wait_event_info based on RAII
// when it goes out of scope.
class PgWaitEventWatcher {
 public:
  using Starter = YbcWaitEventInfo (*)(YbcWaitEventInfo info);

  PgWaitEventWatcher(
      Starter starter, ash::WaitStateCode wait_event, ash::PggateRPC pggate_rpc);
  ~PgWaitEventWatcher();

 private:
  const Starter starter_;
  const YbcWaitEventInfo prev_wait_event_;

  DISALLOW_COPY_AND_ASSIGN(PgWaitEventWatcher);
};

struct EstimatedRowCount {
  int sampledrows;
  double live;
  double dead;
};

struct SampleRandomState {
  double w;
  uint64_t s0;
  uint64_t s1;
};

struct LightweightTableYbctid {
  LightweightTableYbctid(PgOid table_id_, const std::string_view& ybctid_)
      : table_id(table_id_), ybctid(ybctid_) {}
  LightweightTableYbctid(PgOid table_id_, Slice ybctid_)
      : LightweightTableYbctid(table_id_, static_cast<std::string_view>(ybctid_)) {}

  PgOid table_id;
  std::string_view ybctid;
};

struct TableYbctid {
  TableYbctid(PgOid table_id_, std::string ybctid_)
      : table_id(table_id_), ybctid(std::move(ybctid_)) {}

  explicit TableYbctid(const LightweightTableYbctid& lightweight)
      : TableYbctid(lightweight.table_id, std::string(lightweight.ybctid)) {}

  operator LightweightTableYbctid() const {
    return LightweightTableYbctid(table_id, static_cast<std::string_view>(ybctid));
  }

  std::string ToString() const {
    return Format("{ table_id: $0, ybctid: $1 }", table_id, ybctid);
  }

  PgOid table_id;
  std::string ybctid;
};

struct MemoryOptimizedTableYbctid {
  MemoryOptimizedTableYbctid(PgOid table_id_, std::string_view ybctid_);

  operator LightweightTableYbctid() const;

  PgOid table_id;
  uint32_t ybctid_size;
  std::unique_ptr<char[]> ybctid_data;
};

static_assert(
    sizeof(MemoryOptimizedTableYbctid) == 16 &&
    sizeof(MemoryOptimizedTableYbctid) < sizeof(TableYbctid));

struct TableYbctidComparator {
  using is_transparent = void;

  bool operator()(const LightweightTableYbctid& l, const LightweightTableYbctid& r) const {
    return l.table_id == r.table_id && l.ybctid == r.ybctid;
  }
};

struct TableYbctidHasher {
  using is_transparent = void;

  size_t operator()(const LightweightTableYbctid& value) const;
};

template <class T>
using TableYbctidSetHelper =
    std::unordered_set<T, TableYbctidHasher, TableYbctidComparator>;
using MemoryOptimizedTableYbctidSet = TableYbctidSetHelper<MemoryOptimizedTableYbctid>;
using TableYbctidSet = TableYbctidSetHelper<TableYbctid>;

template <class U>
using TableYbctidMap = std::unordered_map<TableYbctid, U, TableYbctidHasher, TableYbctidComparator>;

using ExecParametersMutator = LWFunction<void(YbcPgExecParameters&)>;

struct YbctidBatch {
  YbctidBatch(std::span<const Slice> ybctids_, bool keep_order_)
      : ybctids(ybctids_), keep_order(keep_order_) {}

  std::span<const Slice> ybctids;
  bool keep_order;
};

Slice YbctidAsSlice(const PgTypeInfo& pg_types, uint64_t ybctid);

struct BufferingSettings {
  size_t max_batch_size;
  size_t max_in_flight_operations;
  int multiple;
};

struct YbctidGenerator {
  using Next = LWFunction<Slice()>;

  YbctidGenerator(const Next& next_, size_t capacity_) : next(next_), capacity(capacity_) {}

  const Next& next;
  const size_t capacity;

 private:
  DISALLOW_COPY_AND_ASSIGN(YbctidGenerator);
};

std::string ToString(const YbcAdvisoryLockId& lock_id);
std::string ToString(const YbcObjectLockId& lock_id);

class TablespaceCache {
 public:
  explicit TablespaceCache(size_t capacity);

  std::optional<PgTablespaceOid> Get(PgObjectId table_oid);
  void Put(PgObjectId table_oid, PgTablespaceOid tablespace_oid);
  void Clear();

 private:
  struct Info {
    PgObjectId key;
    mutable PgTablespaceOid tablespace_oid;
  };

  LRUCache<Info, boost::multi_index::member<Info, PgObjectId, &Info::key>> impl_;
};

class TableLocalityMap {
 public:
  void Add(PgOid table_id, const YbcPgTableLocalityInfo& info);
  const YbcPgTableLocalityInfo& Get(PgOid table_id) const;
  void Clear();

 private:
  std::unordered_map<PgOid, YbcPgTableLocalityInfo> map_;
};

} // namespace yb::pggate
