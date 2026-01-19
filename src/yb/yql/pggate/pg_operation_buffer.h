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
#include "yb/yql/pggate/pg_flush_future.h"
#include "yb/yql/pggate/pg_tools.h"

namespace yb::pggate {

class PgFlushDebugContext;
class PgTableDesc;

class BufferableOperations {
 public:
  const PgsqlOps& operations() const { return operations_; }
  const PgObjectIds& relations() const { return relations_; }
  void Add(PgsqlOpPtr&& op, const PgTableDesc& table);
  void Swap(BufferableOperations* rhs);
  void Clear();
  void Reserve(size_t capacity);
  bool Empty() const;
  size_t Size() const;
  void MoveTo(PgsqlOps& operations, PgObjectIds& relations) &&;

  friend std::pair<BufferableOperations, BufferableOperations> Split(
      BufferableOperations&& ops, size_t index);

 private:
  PgsqlOps operations_;
  PgObjectIds relations_;
};

class PgOperationBuffer {
 public:
  using Flusher = std::function<Result<FlushFuture>(
      BufferableOperations&&, bool, const PgFlushDebugContext&)>;

  PgOperationBuffer(Flusher&& flusher, const BufferingSettings& buffering_settings);
  ~PgOperationBuffer();
  Status Add(const PgTableDesc& table, PgsqlWriteOpPtr op, bool transactional);
  Status Flush(const PgFlushDebugContext& dbg_ctx);
  Result<BufferableOperations> Take(bool transactional, const PgFlushDebugContext& dbg_ctx);
  bool IsEmpty() const;
  size_t Size() const;
  void Clear();

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace yb::pggate
