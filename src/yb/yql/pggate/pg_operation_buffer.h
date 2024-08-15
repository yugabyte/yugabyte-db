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

#include <functional>
#include <memory>
#include <utility>

#include "yb/ash/wait_state.h"

#include "yb/common/common_fwd.h"
#include "yb/common/pg_types.h"

#include "yb/util/result.h"
#include "yb/util/status_fwd.h"

#include "yb/yql/pggate/pg_gate_fwd.h"
#include "yb/yql/pggate/pg_perform_future.h"
#include "yb/yql/pggate/pg_tools.h"

namespace yb::pggate {

class PgDocMetrics;
class PgSession;

struct BufferingSettings {
  size_t max_batch_size;
  size_t max_in_flight_operations;
};

struct BufferableOperations {
  PgsqlOps operations;
  PgObjectIds relations;

  void Add(PgsqlOpPtr op, const PgObjectId& relation);
  void Swap(BufferableOperations* rhs);
  void Clear();
  void Reserve(size_t capacity);
  bool empty() const;
  size_t size() const;
};

class PgOperationBuffer {
 public:
  using PerformFutureEx = std::pair<PerformFuture, PgSession*>;
  using OperationsFlusher = std::function<Result<PerformFutureEx>(BufferableOperations&&, bool)>;

  PgOperationBuffer(
      OperationsFlusher&& ops_flusher, PgDocMetrics* metrics,
      PgWaitEventWatcher::Starter wait_starter, const BufferingSettings& buffering_settings);
  ~PgOperationBuffer();
  Status Add(const PgTableDesc& table, PgsqlWriteOpPtr op, bool transactional);
  Status Flush();
  Result<BufferableOperations> FlushTake(
      const PgTableDesc& table, const PgsqlOp& op, bool transactional);
  size_t Size() const;
  void Clear();

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace yb::pggate
