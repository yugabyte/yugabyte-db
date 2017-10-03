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

#include "kudu/tablet/transactions/transaction.h"

namespace kudu {
namespace tablet {

using consensus::DriverType;

Transaction::Transaction(TransactionState* state, DriverType type, TransactionType tx_type)
    : state_(state),
      type_(type),
      tx_type_(tx_type) {
}

TransactionState::TransactionState(TabletPeer* tablet_peer)
    : tablet_peer_(tablet_peer),
      completion_clbk_(new TransactionCompletionCallback()),
      timestamp_error_(0),
      arena_(32 * 1024, 4 * 1024 * 1024),
      external_consistency_mode_(CLIENT_PROPAGATED) {
}

TransactionState::~TransactionState() {
}

TransactionCompletionCallback::TransactionCompletionCallback()
    : code_(tserver::TabletServerErrorPB::UNKNOWN_ERROR) {
}

void TransactionCompletionCallback::set_error(const Status& status,
                                              tserver::TabletServerErrorPB::Code code) {
  status_ = status;
  code_ = code;
}

void TransactionCompletionCallback::set_error(const Status& status) {
  status_ = status;
}

bool TransactionCompletionCallback::has_error() const {
  return !status_.ok();
}

const Status& TransactionCompletionCallback::status() const {
  return status_;
}

const tserver::TabletServerErrorPB::Code TransactionCompletionCallback::error_code() const {
  return code_;
}

void TransactionCompletionCallback::TransactionCompleted() {}

TransactionCompletionCallback::~TransactionCompletionCallback() {}

TransactionMetrics::TransactionMetrics()
  : successful_inserts(0),
    successful_updates(0),
    successful_deletes(0),
    commit_wait_duration_usec(0) {
}

void TransactionMetrics::Reset() {
  successful_inserts = 0;
  successful_updates = 0;
  successful_deletes = 0;
  commit_wait_duration_usec = 0;
}


}  // namespace tablet
}  // namespace kudu
