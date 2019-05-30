//
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
//

#ifndef YB_CLIENT_TRANSACTION_RPC_H
#define YB_CLIENT_TRANSACTION_RPC_H

#include <functional>

#include "yb/client/client_fwd.h"
#include "yb/rpc/rpc_fwd.h"

#include "yb/util/monotime.h"
#include "yb/util/result.h"
#include "yb/util/status.h"

namespace yb {

class HybridTime;

namespace tserver {

class AbortTransactionRequestPB;
class AbortTransactionResponsePB;
class GetTransactionStatusRequestPB;
class GetTransactionStatusResponsePB;
class UpdateTransactionRequestPB;

}

namespace client {

typedef std::function<void(const Status&, HybridTime)> UpdateTransactionCallback;

// Common arguments for all functions from this header.
// deadline - operation deadline, i.e. timeout.
// tablet - handle of status tablet for this transaction, could be null when unknown.
// client - YBClient that should be used to send this request.

// Updates specified transaction.
MUST_USE_RESULT rpc::RpcCommandPtr UpdateTransaction(
    CoarseTimePoint deadline,
    internal::RemoteTablet* tablet,
    YBClient* client,
    tserver::UpdateTransactionRequestPB* req,
    UpdateTransactionCallback callback);

typedef std::function<void(const Status&, const tserver::GetTransactionStatusResponsePB&)>
    GetTransactionStatusCallback;

// Gets status of specified transaction.
MUST_USE_RESULT rpc::RpcCommandPtr GetTransactionStatus(
    CoarseTimePoint deadline,
    internal::RemoteTablet* tablet,
    YBClient* client,
    tserver::GetTransactionStatusRequestPB* req,
    GetTransactionStatusCallback callback);

typedef std::function<void(const Status&, const tserver::AbortTransactionResponsePB&)>
    AbortTransactionCallback;

// Aborts specified transaction.
MUST_USE_RESULT rpc::RpcCommandPtr AbortTransaction(
    CoarseTimePoint deadline,
    internal::RemoteTablet* tablet,
    YBClient* client,
    tserver::AbortTransactionRequestPB* req,
    AbortTransactionCallback callback);

} // namespace client
} // namespace yb

#endif // YB_CLIENT_TRANSACTION_RPC_H
