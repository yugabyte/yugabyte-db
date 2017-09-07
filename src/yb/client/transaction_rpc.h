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

#include "yb/util/monotime.h"
#include "yb/util/status.h"

namespace yb {

namespace tserver {

class GetTransactionStatusRequestPB;
class GetTransactionStatusResponsePB;
class UpdateTransactionRequestPB;

}

namespace client {

class RemoteTablet;
typedef std::function<void(const Status&)> UpdateTransactionCallback;

void UpdateTransaction(const MonoTime& deadline,
                       internal::RemoteTablet* tablet,
                       YBClient* client,
                       tserver::UpdateTransactionRequestPB* req,
                       UpdateTransactionCallback callback);

typedef std::function<void(const Status&, const tserver::GetTransactionStatusResponsePB&)>
    GetTransactionStatusCallback;

void GetTransactionStatus(const MonoTime& deadline,
                          internal::RemoteTablet* tablet,
                          YBClient* client,
                          tserver::GetTransactionStatusRequestPB* req,
                          GetTransactionStatusCallback callback);

} // namespace client
} // namespace yb

#endif // YB_CLIENT_TRANSACTION_RPC_H
