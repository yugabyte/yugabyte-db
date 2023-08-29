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

#include <float.h>
#include <stdint.h>

#include <chrono>
#include <memory>
#include <sstream>
#include <string>
#include <type_traits>

#include <boost/container/small_vector.hpp>
#include <boost/mpl/and.hpp>

#include "yb/util/format.h"
#include "yb/util/memory/memory_usage.h"
#include "yb/util/ref_cnt_buffer.h"
#include "yb/util/tostring.h"
#include "yb/util/type_traits.h"
#include "yb/rpc/reactor_thread_role.h"

namespace yb {

class Status;

namespace rpc {

class Connection;
class DumpRunningRpcsRequestPB;
class RpcCallInProgressPB;

// Interface for outbound transfers from the RPC framework. Implementations include:
// - RpcCall
// - LocalOutboundCall
// - ConnectionHeader
// - ServerEventList
class OutboundData : public std::enable_shared_from_this<OutboundData> {
 public:
  virtual void Transferred(const Status& status, const ConnectionPtr& conn) = 0;

  // Serializes the data to be sent out via the RPC framework.
  virtual void Serialize(boost::container::small_vector_base<RefCntBuffer>* output) = 0;

  virtual std::string ToString() const = 0;

  virtual bool DumpPB(const DumpRunningRpcsRequestPB& req, RpcCallInProgressPB* resp) = 0;

  virtual bool IsFinished() const { return false; }

  virtual bool IsHeartbeat() const { return false; }

  virtual size_t ObjectSize() const = 0;

  virtual size_t DynamicMemoryUsage() const = 0;

  virtual ~OutboundData() {}
};

typedef std::shared_ptr<OutboundData> OutboundDataPtr;

class StringOutboundData : public OutboundData {
 public:
  StringOutboundData(const std::string& data, const std::string& name)
      : buffer_(data), name_(name) {}
  StringOutboundData(const char* data, size_t len, const std::string& name)
      : buffer_(data, len), name_(name) {}
  void Transferred(const Status& status, const ConnectionPtr& conn) override {}

  // Serializes the data to be sent out via the RPC framework.
  void Serialize(boost::container::small_vector_base<RefCntBuffer>* output) override {
    output->push_back(buffer_);
  }

  std::string ToString() const override { return name_; }

  bool DumpPB(const DumpRunningRpcsRequestPB& req, RpcCallInProgressPB* resp) override {
    return false;
  }

  size_t ObjectSize() const override { return sizeof(*this); }

  size_t DynamicMemoryUsage() const override { return DynamicMemoryUsageOf(name_, buffer_); }

 private:
  RefCntBuffer buffer_;
  std::string name_;
};

// OutboundData wrapper, that is used for altered streams, where we modify the data that should be
// sent. Examples could be that we encrypt or compress it.
// This wrapper would contain modified data and reference to original data, that will be used
// for notifications.
class SingleBufferOutboundData : public OutboundData {
 public:
  SingleBufferOutboundData(RefCntBuffer buffer, OutboundDataPtr lower_data)
      : buffer_(std::move(buffer)), lower_data_(std::move(lower_data)) {}

  void Transferred(const Status& status, const ConnectionPtr& conn) override {
    if (lower_data_) {
      lower_data_->Transferred(status, conn);
    }
  }

  bool DumpPB(const DumpRunningRpcsRequestPB& req, RpcCallInProgressPB* resp) override {
    return false;
  }

  void Serialize(boost::container::small_vector_base<RefCntBuffer>* output) override {
    output->push_back(std::move(buffer_));
  }

  std::string ToString() const override {
    return Format("SingleBuffer[$0]", lower_data_);
  }

  size_t ObjectSize() const override { return sizeof(*this); }

  size_t DynamicMemoryUsage() const override { return DynamicMemoryUsageOf(buffer_, lower_data_); }

 private:
  RefCntBuffer buffer_;
  OutboundDataPtr lower_data_;
};

}  // namespace rpc
}  // namespace yb

#endif // YB_RPC_OUTBOUND_DATA_H
