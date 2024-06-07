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

#pragma once

#include <stdint.h>

#include <vector>

#include "yb/rpc/rpc_call.h"

#include "yb/util/slice.h"

namespace yb {
namespace rpc {

class ServerEvent {
 public:
  virtual ~ServerEvent() {}
  // Serializes the data to be sent out via the RPC framework.
  virtual void Serialize(ByteBlocks* output) const = 0;
  virtual std::string ToString() const = 0;
};

class ServerEventList : public OutboundData {
 public:
  virtual ~ServerEventList() {}

  bool DumpPB(const DumpRunningRpcsRequestPB& req, RpcCallInProgressPB* resp) override {
    return false;
  }
};

}  // namespace rpc
}  // namespace yb
