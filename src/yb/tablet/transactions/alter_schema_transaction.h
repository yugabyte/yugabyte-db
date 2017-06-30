// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.

#ifndef YB_TABLET_TRANSACTIONS_ALTER_SCHEMA_TRANSACTION_H_
#define YB_TABLET_TRANSACTIONS_ALTER_SCHEMA_TRANSACTION_H_

#include <mutex>
#include <string>

#include "yb/gutil/macros.h"
#include "yb/tablet/transactions/transaction.h"
#include "yb/util/locks.h"

namespace yb {

class Schema;

namespace consensus {
class Consensus;
}

namespace tablet {

// Transaction Context for the AlterSchema operation.
// Keeps track of the Transaction states (request, result, ...)
class AlterSchemaTransactionState : public TransactionState {
 public:
  ~AlterSchemaTransactionState() {
  }

  AlterSchemaTransactionState(TabletPeer* tablet_peer,
                              const tserver::AlterSchemaRequestPB* request = nullptr,
                              tserver::AlterSchemaResponsePB* response = nullptr)
      : TransactionState(tablet_peer),
        schema_(nullptr),
        request_(request),
        response_(response) {
  }

  const tserver::AlterSchemaRequestPB* request() const override { return request_; }
  void UpdateRequestFromConsensusRound() override {
    request_ = consensus_round()->replicate_msg()->mutable_alter_schema_request();
  }
  tserver::AlterSchemaResponsePB* response() override { return response_; }

  void set_schema(const Schema* schema) { schema_ = schema; }
  const Schema* schema() const { return schema_; }

  std::string new_table_name() const {
    return request_->new_table_name();
  }

  bool has_new_table_name() const {
    return request_->has_new_table_name();
  }

  uint32_t schema_version() const {
    return request_->schema_version();
  }

  void AcquireSchemaLock(rw_semaphore* l);

  // Release the acquired schema lock.
  // Crashes if the lock was not already acquired.
  void ReleaseSchemaLock();

  // Note: request_ and response_ are set to NULL after this method returns.
  void Finish() {
    // Make the request NULL since after this transaction commits
    // the request may be deleted at any moment.
    request_ = NULL;
    response_ = NULL;
  }

  virtual std::string ToString() const override;

 private:

  // The new (target) Schema.
  const Schema* schema_;

  // The original RPC request and response.
  const tserver::AlterSchemaRequestPB *request_;
  tserver::AlterSchemaResponsePB *response_;

  // The lock held on the tablet's schema_lock_.
  std::unique_lock<rw_semaphore> schema_lock_;

  DISALLOW_COPY_AND_ASSIGN(AlterSchemaTransactionState);
};

// Executes the alter schema transaction,.
class AlterSchemaTransaction : public Transaction {
 public:
  AlterSchemaTransaction(AlterSchemaTransactionState* tx_state, consensus::DriverType type);

  virtual AlterSchemaTransactionState* state() override { return state_.get(); }
  virtual const AlterSchemaTransactionState* state() const override { return state_.get(); }

  consensus::ReplicateMsgPtr NewReplicateMsg() override;

  // Executes a Prepare for the alter schema transaction.
  //
  // TODO: need a schema lock?

  virtual CHECKED_STATUS Prepare() override;

  // Starts the AlterSchemaTransaction by assigning it a timestamp.
  virtual void Start() override;

  // Executes an Apply for the alter schema transaction
  virtual CHECKED_STATUS Apply(gscoped_ptr<consensus::CommitMsg>* commit_msg) override;

  // Actually commits the transaction.
  virtual void Finish(TransactionResult result) override;

  virtual std::string ToString() const override;

 private:
  gscoped_ptr<AlterSchemaTransactionState> state_;
  DISALLOW_COPY_AND_ASSIGN(AlterSchemaTransaction);
};

}  // namespace tablet
}  // namespace yb

#endif  // YB_TABLET_TRANSACTIONS_ALTER_SCHEMA_TRANSACTION_H_
