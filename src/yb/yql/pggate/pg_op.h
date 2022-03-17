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

#ifndef YB_YQL_PGGATE_PG_OP_H
#define YB_YQL_PGGATE_PG_OP_H

#include "yb/util/status.h"

#include "yb/common/common_fwd.h"
#include "yb/common/pgsql_protocol.pb.h"
#include "yb/common/read_hybrid_time.h"

#include "yb/rpc/rpc_fwd.h"

#include "yb/yql/pggate/pg_gate_fwd.h"

namespace yb {
namespace pggate {

class PgsqlOp {
 public:
  PgsqlOp() = default;
  virtual ~PgsqlOp() = default;

  PgsqlOp(const PgsqlOp&) = delete;
  void operator=(const PgsqlOp&) = delete;

  virtual bool is_read() const = 0;

  virtual bool need_transaction() const = 0;

  bool is_write() const {
    return !is_read();
  }

  PgsqlResponsePB& response() {
    return response_;
  }

  const PgsqlResponsePB& response() const {
    return response_;
  }

  rpc::SidecarPtr& rows_data() {
    return rows_data_;
  }

  bool is_active() const {
    return active_;
  }

  void set_active(bool value) {
    active_ = value;
  }

  void set_read_time(const ReadHybridTime& value) {
    read_time_ = value;
  }

  const ReadHybridTime& read_time() const {
    return read_time_;
  }

  std::string ToString() const;

  virtual CHECKED_STATUS InitPartitionKey(const PgTableDesc& table) = 0;

 private:
  virtual std::string RequestToString() const = 0;

  bool active_ = false;
  PgsqlResponsePB response_;
  rpc::SidecarPtr rows_data_;
  ReadHybridTime read_time_;
};

class PgsqlReadOp : public PgsqlOp {
 public:
  PgsqlReadOp() = default;
  explicit PgsqlReadOp(const PgTableDesc& desc);

  PgsqlReadRequestPB& read_request() {
    return read_request_;
  }

  const PgsqlReadRequestPB& read_request() const {
    return read_request_;
  }

  bool is_read() const override {
    return true;
  }

  bool need_transaction() const override {
    return true;
  }

  void set_read_from_followers() {
    read_from_followers_ = true;
  }

  bool read_from_followers() const {
    return read_from_followers_;
  }

  PgsqlOpPtr DeepCopy() const {
    auto result = std::make_shared<PgsqlReadOp>();
    result->read_request() = read_request();
    result->read_from_followers_ = read_from_followers_;
    return result;
  }

  std::string RequestToString() const override;

 private:
  CHECKED_STATUS InitPartitionKey(const PgTableDesc& table) override;

  PgsqlReadRequestPB read_request_;
  bool read_from_followers_ = false;
};

using PgsqlReadOpPtr = std::shared_ptr<PgsqlReadOp>;

std::shared_ptr<PgsqlReadRequestPB> InitSelect(
    const PgsqlReadOpPtr& read_op, const PgTableDesc& desc);

class PgsqlWriteOp : public PgsqlOp {
 public:
  explicit PgsqlWriteOp(bool need_transaction) : need_transaction_(need_transaction) {}

  PgsqlWriteRequestPB& write_request() {
    return write_request_;
  }

  const PgsqlWriteRequestPB& write_request() const {
    return write_request_;
  }

  bool is_read() const override {
    return false;
  }

  bool need_transaction() const override {
    return need_transaction_;
  }

  PgsqlOpPtr DeepCopy() const {
    auto result = std::make_shared<PgsqlWriteOp>(need_transaction_);
    result->write_request() = write_request();
    return result;
  }

  HybridTime write_time() const {
    return write_time_;
  }

  void SetWriteTime(HybridTime value) {
    write_time_ = value;
  }

  std::string RequestToString() const override;

 private:
  CHECKED_STATUS InitPartitionKey(const PgTableDesc& table) override;

  PgsqlWriteRequestPB write_request_;
  bool need_transaction_;
  HybridTime write_time_;
};

using PgsqlWriteOpPtr = std::shared_ptr<PgsqlWriteOp>;

CHECKED_STATUS ReviewResponsePagingState(const PgTableDesc& table, PgsqlReadOp* op);

}  // namespace pggate
}  // namespace yb

#endif  // YB_YQL_PGGATE_PG_OP_H
