//
// Copyright (c) YugaByte, Inc.
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
