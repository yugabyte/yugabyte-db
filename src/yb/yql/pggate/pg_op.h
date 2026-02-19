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

#include "yb/util/status.h"

#include "yb/common/common_fwd.h"
#include "yb/common/pgsql_protocol.messages.h"
#include "yb/common/read_hybrid_time.h"

#include "yb/rpc/rpc_fwd.h"

#include "yb/yql/pggate/pg_gate_fwd.h"
#include "yb/yql/pggate/ybc_pg_typedefs.h"

namespace yb::pggate {

class PgsqlOp {
 public:
  PgsqlOp(ThreadSafeArena* arena, const YbcPgTableLocalityInfo& locality_info)
      : arena_(arena), locality_info_(locality_info) {}
  virtual ~PgsqlOp() = default;

  PgsqlOp(const PgsqlOp&) = delete;
  void operator=(const PgsqlOp&) = delete;

  virtual bool is_read() const = 0;

  virtual bool need_transaction() const = 0;

  bool is_write() const {
    return !is_read();
  }

  ThreadSafeArena& arena() const {
    return *arena_;
  }

  void set_response(LWPgsqlResponsePB* response) {
    response_ = response;
  }

  LWPgsqlResponsePB* response() {
    return response_;
  }

  const LWPgsqlResponsePB* response() const {
    return response_;
  }

  bool is_active() const {
    return active_;
  }

  void set_active(bool value) {
    active_ = value;
  }

  const YbcPgTableLocalityInfo& locality_info() const {
    return locality_info_;
  }

  void set_read_time(const ReadHybridTime& value) {
    read_time_ = value;
  }

  const ReadHybridTime& read_time() const {
    return read_time_;
  }

  // Merge streams produce ordered results whether tablet split took place or not.
  // Therefore if the request paginates fetch routine can append the response pages
  // to the same operation's stream.
  void set_is_merge_stream(bool value) {
    is_merge_stream_ = value;
  }

  bool is_merge_stream() const {
    return is_merge_stream_;
  }

  std::string ToString() const;

  virtual Status InitPartitionKey(const PgTableDesc& table) = 0;

  virtual Status ConvertBoundsToHashCode() = 0;

 private:
  virtual std::string RequestToString() const = 0;

  // dtor for this class is not invoked, so only fields that could be destroyed with arena are
  // allowed.
  ThreadSafeArena* arena_;
  bool active_ = false;
  const YbcPgTableLocalityInfo locality_info_;
  LWPgsqlResponsePB* response_ = nullptr;
  ReadHybridTime read_time_;
  bool is_merge_stream_ = false;
};

class PgsqlReadOp : public PgsqlOp {
 public:
  PgsqlReadOp(ThreadSafeArena* arena, const YbcPgTableLocalityInfo& locality_info);
  PgsqlReadOp(
      ThreadSafeArena* arena, const PgTableDesc& desc, const YbcPgTableLocalityInfo& locality_info,
      PgsqlMetricsCaptureType metrics_capture);

  LWPgsqlReadRequestPB& read_request() {
    return read_request_;
  }

  const LWPgsqlReadRequestPB& read_request() const {
    return read_request_;
  }

  bool is_read() const override {
    return true;
  }

  bool need_transaction() const override {
    return true;
  }

  PgsqlOpPtr DeepCopy(const std::shared_ptr<ThreadSafeArena>& arena_ptr) const;

  std::string RequestToString() const override;

 private:
  Status InitPartitionKey(const PgTableDesc& table) override;
  Status ConvertBoundsToHashCode() override;
  // Check if lower_bound/upper_bound are derived from hash code using HashCodeToDocKeyBound().
  Result<bool> BoundsDerivedFromHashCode();
  void OverrideBoundWithHashCode(uint16_t hash_code, bool is_lower);

  LWPgsqlReadRequestPB read_request_;
};

std::shared_ptr<PgsqlReadRequestPB> InitSelect(
    const PgsqlReadOpPtr& read_op, const PgTableDesc& desc);

class PgsqlWriteOp : public PgsqlOp {
 public:
  PgsqlWriteOp(
      ThreadSafeArena* arena, bool need_transaction, const YbcPgTableLocalityInfo& locality_info);

  LWPgsqlWriteRequestPB& write_request() {
    return write_request_;
  }

  const LWPgsqlWriteRequestPB& write_request() const {
    return write_request_;
  }

  bool is_read() const override {
    return false;
  }

  bool need_transaction() const override {
    return need_transaction_;
  }

  PgsqlOpPtr DeepCopy(const std::shared_ptr<void>& shared_ptr) const;

  HybridTime write_time() const {
    return write_time_;
  }

  void SetWriteTime(HybridTime value) {
    write_time_ = value;
  }

  std::string RequestToString() const override;

 private:
  Status InitPartitionKey(const PgTableDesc& table) override;
  Status ConvertBoundsToHashCode() override {
    LOG(DFATAL) << "Not applicable to write ops";
    return Status::OK();
  }

  LWPgsqlWriteRequestPB write_request_;
  bool need_transaction_;
  HybridTime write_time_;
};


Result<bool> PrepareNextRequest(const PgTableDesc& table, PgsqlReadOp* read_op);

inline auto GetSharedArena(const PgsqlOpPtr& op) {
  return std::shared_ptr<ThreadSafeArena>(op, &op->arena());
}

}  // namespace yb::pggate
