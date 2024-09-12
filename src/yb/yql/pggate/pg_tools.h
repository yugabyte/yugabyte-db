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
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include "yb/ash/wait_state.h"

#include "yb/common/pg_types.h"
#include "yb/common/transaction.pb.h"

#include "yb/gutil/macros.h"
#include "yb/gutil/ref_counted.h"

#include "yb/util/lw_function.h"
#include "yb/util/slice.h"
#include "yb/util/status.h"

struct PgExecParameters;

namespace yb::pggate {

class PgSession;

RowMarkType GetRowMarkType(const PgExecParameters* exec_params);

struct Bound {
  uint16_t value;
  bool is_inclusive;
};

// A helper to set the specified wait_event_info in the specified
// MyProc and revert to the previous wait_event_info based on RAII
// when it goes out of scope.
class PgWaitEventWatcher {
 public:
  using Starter = uint32_t (*)(uint32_t wait_event);

  PgWaitEventWatcher(Starter starter, ash::WaitStateCode wait_event);
  ~PgWaitEventWatcher();

 private:
  const Starter starter_;
  const uint32_t prev_wait_event_;

  DISALLOW_COPY_AND_ASSIGN(PgWaitEventWatcher);
};

struct EstimatedRowCount {
  double live;
  double dead;
};

struct LightweightTableYbctid {
  LightweightTableYbctid(PgOid table_id_, const std::string_view& ybctid_)
      : table_id(table_id_), ybctid(ybctid_) {}
  LightweightTableYbctid(PgOid table_id_, const Slice& ybctid_)
      : LightweightTableYbctid(table_id_, static_cast<std::string_view>(ybctid_)) {}

  PgOid table_id;
  std::string_view ybctid;
};

struct TableYbctid {
  TableYbctid(PgOid table_id_, std::string ybctid_)
      : table_id(table_id_), ybctid(std::move(ybctid_)) {}

  operator LightweightTableYbctid() const {
    return LightweightTableYbctid(table_id, static_cast<std::string_view>(ybctid));
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

using OidSet = std::unordered_set<PgOid>;
template <class T>
using TableYbctidSetHelper =
    std::unordered_set<T, TableYbctidHasher, TableYbctidComparator>;
using MemoryOptimizedTableYbctidSet = TableYbctidSetHelper<MemoryOptimizedTableYbctid>;
using TableYbctidSet = TableYbctidSetHelper<TableYbctid>;
using TableYbctidVector = std::vector<TableYbctid>;

class TableYbctidVectorProvider {
 public:
  class Accessor {
   public:
    ~Accessor() { container_.clear(); }
    TableYbctidVector* operator->() { return &container_; }
    TableYbctidVector& operator*() { return container_; }
    operator TableYbctidVector&() { return container_; }

   private:
    explicit Accessor(TableYbctidVector* container) : container_(*container) {}

    friend class TableYbctidVectorProvider;

    TableYbctidVector& container_;

    DISALLOW_COPY_AND_ASSIGN(Accessor);
  };

  [[nodiscard]] Accessor Get() { return Accessor(&container_); }

 private:
  TableYbctidVector container_;
};

using ExecParametersMutator = LWFunction<void(PgExecParameters&)>;

using YbctidReader =
    std::function<Status(PgOid, TableYbctidVector&, const OidSet&, const ExecParametersMutator&)>;

Status FetchExistingYbctids(
    const scoped_refptr<PgSession>& session, PgOid database_id, TableYbctidVector& ybctids,
    const OidSet& region_local_tables, const ExecParametersMutator& exec_params_mutator);

} // namespace yb::pggate
