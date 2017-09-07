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

#ifndef YB_RPC_OUTBOUND_DATA_H
#define YB_RPC_OUTBOUND_DATA_H

#include <deque>
#include <memory>

#include "yb/util/ref_cnt_buffer.h"

namespace yb {

class Status;

namespace rpc {

// Interface for outbound transfers from the RPC framework.
class OutboundData : public std::enable_shared_from_this<OutboundData> {
 public:
  virtual void Transferred(const Status& status) = 0;

  virtual ~OutboundData() {}
  // Serializes the data to be sent out via the RPC framework.
  virtual void Serialize(std::deque<RefCntBuffer> *output) const = 0;
  virtual std::string ToString() const = 0;
};

typedef std::shared_ptr<OutboundData> OutboundDataPtr;

}  // namespace rpc
}  // namespace yb

#endif // YB_RPC_OUTBOUND_DATA_H
