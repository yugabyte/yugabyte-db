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

#include <memory>

#include <boost/container/small_vector.hpp>

#include "yb/util/ref_cnt_buffer.h"

namespace yb {

class Status;

namespace rpc {

class DumpRunningRpcsRequestPB;
class RpcCallInProgressPB;

// Interface for outbound transfers from the RPC framework. Implementations include:
// - RpcCall
// - LocalOutboundCall
// - ConnectionHeader
// - ServerEventList
class OutboundData : public std::enable_shared_from_this<OutboundData> {
 public:
  virtual void Transferred(const Status& status, Connection* conn) = 0;

  // Serializes the data to be sent out via the RPC framework.
  virtual void Serialize(boost::container::small_vector_base<RefCntBuffer>* output) = 0;

  virtual std::string ToString() const = 0;

  virtual bool DumpPB(const DumpRunningRpcsRequestPB& req, RpcCallInProgressPB* resp) = 0;

  virtual bool IsFinished() const { return false; }

  virtual bool IsHeartbeat() const { return false; }

  virtual ~OutboundData() {}
};

typedef std::shared_ptr<OutboundData> OutboundDataPtr;

class StringOutboundData : public OutboundData {
 public:
  StringOutboundData(const string& data, const string& name) : buffer_(data), name_(name) {}
  StringOutboundData(const char* data, size_t len, const string& name)
      : buffer_(data, len), name_(name) {}
  void Transferred(const Status& status, Connection* conn) override {}

  // Serializes the data to be sent out via the RPC framework.
  void Serialize(boost::container::small_vector_base<RefCntBuffer>* output) override {
    output->push_back(buffer_);
  }

  std::string ToString() const override { return name_; }

  bool DumpPB(const DumpRunningRpcsRequestPB& req, RpcCallInProgressPB* resp) override {
    return false;
  }

 private:
  RefCntBuffer buffer_;
  string name_;
};

}  // namespace rpc
}  // namespace yb

#endif // YB_RPC_OUTBOUND_DATA_H
