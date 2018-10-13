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

// No include guards here because this file is expected to be included multiple times.

#ifdef YBC_CXX_DECLARATION_MODE
#include <mutex>

#include "yb/gutil/macros.h"
#include "yb/client/client_fwd.h"
#include "yb/client/transaction_manager.h"
#include "yb/common/clock.h"
#include "yb/gutil/ref_counted.h"
#endif  // YBC_CXX_DECLARATION_MODE

#ifdef YBC_CXX_DECLARATION_MODE
namespace yb {
namespace pggate {
#endif  // YBC_CXX_DECLARATION_MODE

#define YBC_CURRENT_CLASS PgTxnManager

YBC_CLASS_START_REF_COUNTED_THREAD_SAFE

YBC_VIRTUAL_DESTRUCTOR

YBC_STATUS_METHOD_NO_ARGS(BeginTransaction)
YBC_STATUS_METHOD_NO_ARGS(CommitTransaction)
YBC_STATUS_METHOD_NO_ARGS(AbortTransaction)

#ifdef YBC_CXX_DECLARATION_MODE
  PgTxnManager(client::AsyncClientInitialiser* async_client_init,
               scoped_refptr<ClockBase> clock);

  // Returns the transactional session, or nullptr otherwise.
  client::YBSession* GetTransactionalSession();

  Status BeginWriteTransactionIfNecessary();

 private:

  client::TransactionManager* GetOrCreateTransactionManager();
  void ResetTxnAndSession();
  void StartNewSession();

  bool txn_in_progress_ = false;
  client::YBTransactionPtr txn_;
  client::YBSessionPtr session_;

  client::AsyncClientInitialiser* async_client_init_ = nullptr;
  scoped_refptr<ClockBase> clock_;
  std::atomic<client::TransactionManager*> transaction_manager_{nullptr};
  std::mutex transaction_manager_mutex_;
  std::unique_ptr<client::TransactionManager> transaction_manager_holder_;

  DISALLOW_COPY_AND_ASSIGN(PgTxnManager);
#endif  // YBC_CXX_DECLARATION_MODE

YBC_CLASS_END

#undef YBC_CURRENT_CLASS

#ifdef YBC_CXX_DECLARATION_MODE
}  // namespace pggate
}  // namespace yb
#endif  // YBC_CXX_DECLARATION_MODE
