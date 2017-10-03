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

#ifndef KUDU_TABLET_ALTER_SCHEMA_TRANSACTION_H_
#define KUDU_TABLET_ALTER_SCHEMA_TRANSACTION_H_

#include <mutex>
#include <string>

#include "kudu/gutil/macros.h"
#include "kudu/tablet/transactions/transaction.h"
#include "kudu/util/locks.h"

namespace kudu {

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
                              const tserver::AlterSchemaRequestPB* request,
                              tserver::AlterSchemaResponsePB* response)
      : TransactionState(tablet_peer),
        schema_(NULL),
        request_(request),
        response_(response) {
  }

  const tserver::AlterSchemaRequestPB* request() const OVERRIDE { return request_; }
  tserver::AlterSchemaResponsePB* response() OVERRIDE { return response_; }

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

  virtual std::string ToString() const OVERRIDE;

 private:
  DISALLOW_COPY_AND_ASSIGN(AlterSchemaTransactionState);

  // The new (target) Schema.
  const Schema* schema_;

  // The original RPC request and response.
  const tserver::AlterSchemaRequestPB *request_;
  tserver::AlterSchemaResponsePB *response_;

  // The lock held on the tablet's schema_lock_.
  std::unique_lock<rw_semaphore> schema_lock_;
};

// Executes the alter schema transaction,.
class AlterSchemaTransaction : public Transaction {
 public:
  AlterSchemaTransaction(AlterSchemaTransactionState* tx_state, consensus::DriverType type);

  virtual AlterSchemaTransactionState* state() OVERRIDE { return state_.get(); }
  virtual const AlterSchemaTransactionState* state() const OVERRIDE { return state_.get(); }

  void NewReplicateMsg(gscoped_ptr<consensus::ReplicateMsg>* replicate_msg) OVERRIDE;

  // Executes a Prepare for the alter schema transaction.
  //
  // TODO: need a schema lock?

  virtual Status Prepare() OVERRIDE;

  // Starts the AlterSchemaTransaction by assigning it a timestamp.
  virtual Status Start() OVERRIDE;

  // Executes an Apply for the alter schema transaction
  virtual Status Apply(gscoped_ptr<consensus::CommitMsg>* commit_msg) OVERRIDE;

  // Actually commits the transaction.
  virtual void Finish(TransactionResult result) OVERRIDE;

  virtual std::string ToString() const OVERRIDE;

 private:
  gscoped_ptr<AlterSchemaTransactionState> state_;
  DISALLOW_COPY_AND_ASSIGN(AlterSchemaTransaction);
};

}  // namespace tablet
}  // namespace kudu

#endif /* KUDU_TABLET_ALTER_SCHEMA_TRANSACTION_H_ */
