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

#include <boost/preprocessor/seq/for_each.hpp>

#include "yb/client/client_fwd.h"
#include "yb/rpc/rpc_fwd.h"

#include "yb/util/monotime.h"
#include "yb/util/result.h"
#include "yb/util/status.h"

namespace yb {

class HybridTime;

#define TRANSACTION_RPCS \
    (UpdateTransaction) \
    (GetTransactionStatus) \
    (GetTransactionStatusAtParticipant) \
    (AbortTransaction)

namespace tserver {

#define TRANSACTION_RPC_TSERVER_FORWARD(i, data, entry) \
  class BOOST_PP_CAT(entry, RequestPB); \
  class BOOST_PP_CAT(entry, ResponsePB);

BOOST_PP_SEQ_FOR_EACH(TRANSACTION_RPC_TSERVER_FORWARD, ~, TRANSACTION_RPCS)

}

namespace client {

// Common arguments for all functions from this header.
// deadline - operation deadline, i.e. timeout.
// tablet - handle of status tablet for this transaction, could be null when unknown.
// client - YBClient that should be used to send this request.

#define TRANSACTION_RPC_CALLBACK(rpc) BOOST_PP_CAT(rpc, Callback)
#define TRANSACTION_RPC_REQUEST_PB(rpc) tserver::BOOST_PP_CAT(rpc, RequestPB)
#define TRANSACTION_RPC_RESPONSE_PB(rpc) tserver::BOOST_PP_CAT(rpc, ResponsePB)

#define TRANSACTION_RPC_SEMICOLON(rpc) ; // NOLINT

#define TRANSACTION_RPC_FUNCTION(i, data, entry) \
  typedef std::function<void(const Status&, const TRANSACTION_RPC_RESPONSE_PB(entry)&)> \
      TRANSACTION_RPC_CALLBACK(entry); \
  MUST_USE_RESULT rpc::RpcCommandPtr entry( \
      CoarseTimePoint deadline, \
      internal::RemoteTablet* tablet, \
      YBClient* client, \
      TRANSACTION_RPC_REQUEST_PB(entry)* req, \
      TRANSACTION_RPC_CALLBACK(entry) callback) data(entry) \

BOOST_PP_SEQ_FOR_EACH(TRANSACTION_RPC_FUNCTION, TRANSACTION_RPC_SEMICOLON, TRANSACTION_RPCS)

template <class Response, class T>
void UpdateClock(const Response& resp, T* t) {
  if (resp.has_propagated_hybrid_time()) {
    t->UpdateClock(HybridTime(resp.propagated_hybrid_time()));
  }
}

} // namespace client
} // namespace yb

#endif // YB_CLIENT_TRANSACTION_RPC_H
