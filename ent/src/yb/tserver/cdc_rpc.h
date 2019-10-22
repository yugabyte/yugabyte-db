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

#ifndef ENT_SRC_YB_TSERVER_CDC_RPC_H
#define ENT_SRC_YB_TSERVER_CDC_RPC_H

#include <functional>

#include "yb/client/client_fwd.h"
#include "yb/rpc/rpc.h"

#include "yb/util/result.h"
#include "yb/util/status.h"

namespace yb {

namespace cdc {

class CDCRecordPB;

} // namespace cdc

namespace tserver {

class WriteRequestPB;
class WriteResponsePB;

namespace enterprise {

typedef std::function<void(const Status&, const WriteResponsePB&)> WriteCDCRecordCallback;

// deadline - operation deadline, i.e. timeout.
// tablet - tablet to write the CDC record to.
// client - YBClient that should be used to send this request.
// Writes CDC record.
MUST_USE_RESULT rpc::RpcCommandPtr WriteCDCRecord(
    CoarseTimePoint deadline,
    client::internal::RemoteTablet* tablet,
    client::YBClient* client,
    WriteRequestPB* req,
    WriteCDCRecordCallback callback);

} // namespace enterprise
} // namespace tserver
} // namespace yb

#endif // ENT_SRC_YB_TSERVER_CDC_RPC_H
