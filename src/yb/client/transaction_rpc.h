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

#pragma once

#include <functional>

#include <boost/preprocessor/empty.hpp>
#include <boost/preprocessor/seq/for_each.hpp>

#include "yb/client/client_fwd.h"
#include "yb/rpc/rpc_fwd.h"

#include "yb/tserver/tserver_fwd.h"

#include "yb/util/monotime.h"
#include "yb/util/status_fwd.h"

namespace yb {

class HybridTime;

#define TRANSACTION_RPCS \
    ((UpdateTransaction, WITH_REQUEST)) \
    ((GetTransactionStatus, WITHOUT_REQUEST)) \
    ((GetTransactionStatusAtParticipant, WITHOUT_REQUEST)) \
    ((AbortTransaction, WITHOUT_REQUEST)) \
    ((UpdateTransactionStatusLocation, WITHOUT_REQUEST)) \
    ((UpdateTransactionWaitingForStatus, WITHOUT_REQUEST)) \
    ((ProbeTransactionDeadlock, WITH_REQUEST))

#define TRANSACTION_RPC_NAME(entry) BOOST_PP_TUPLE_ELEM(2, 0, entry)

namespace client {

// Common arguments for all functions from this header.
// deadline - operation deadline, i.e. timeout.
// tablet - handle of status tablet for this transaction, could be null when unknown.
// client - YBClient that should be used to send this request.

#define TRANSACTION_RPC_CALLBACK(rpc) BOOST_PP_CAT(TRANSACTION_RPC_NAME(rpc), Callback)
#define TRANSACTION_RPC_REQUEST_PB(rpc) tserver::BOOST_PP_CAT(TRANSACTION_RPC_NAME(rpc), RequestPB)
#define TRANSACTION_RPC_RESPONSE_PB(rpc) \
    tserver::BOOST_PP_CAT(TRANSACTION_RPC_NAME(rpc), ResponsePB)

#define TRANSACTION_RPC_OPTIONAL_REQUEST_HELPER_WITH_REQUEST(entry) \
  const TRANSACTION_RPC_REQUEST_PB(entry)&,

#define TRANSACTION_RPC_OPTIONAL_REQUEST_HELPER_WITHOUT_REQUEST(entry)

#define TRANSACTION_RPC_OPTIONAL_REQUEST_PB(entry) \
  BOOST_PP_CAT(TRANSACTION_RPC_OPTIONAL_REQUEST_HELPER_, BOOST_PP_TUPLE_ELEM(2, 1, entry))(entry)

#define TRANSACTION_RPC_SEMICOLON(rpc) ; // NOLINT

#define TRANSACTION_RPC_FUNCTION(i, data, entry) \
  using TRANSACTION_RPC_CALLBACK(entry) = std::function<void( \
      const Status&,                             \
      TRANSACTION_RPC_OPTIONAL_REQUEST_PB(entry) \
      const TRANSACTION_RPC_RESPONSE_PB(entry)&)>; \
  MUST_USE_RESULT rpc::RpcCommandPtr TRANSACTION_RPC_NAME(entry)( \
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
