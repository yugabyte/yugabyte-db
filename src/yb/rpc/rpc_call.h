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

#ifndef YB_RPC_RPC_CALL_H
#define YB_RPC_RPC_CALL_H

#include "yb/gutil/ref_counted.h"

#include "yb/util/enums.h"

#include "yb/rpc/outbound_data.h"

namespace yb {

class Status;

namespace rpc {

YB_DEFINE_ENUM(TransferState, (PENDING)(FINISHED)(ABORTED));

class RpcCall : public OutboundData {
 public:
  // This functions is invoked in reactor thread of appropriate connection.
  // So it doesn't require synchronization.
  void Transferred(const Status& status) override;

 private:
  virtual void NotifyTransferred(const Status& status) = 0;

  TransferState state_ = TransferState::PENDING;
};

}  // namespace rpc
}  // namespace yb

#endif // YB_RPC_RPC_CALL_H
