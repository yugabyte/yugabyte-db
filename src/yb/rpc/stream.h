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

#include "yb/rpc/rpc_fwd.h"
#include "yb/rpc/reactor_thread_role.h"

#include "yb/util/status_fwd.h"
#include "yb/util/net/socket.h"

namespace ev {

struct loop_ref;

}

namespace yb {

class MemTracker;
class MetricEntity;

namespace rpc {

using CallHandle = size_t;

// A value we use instead of a call handle in case there is a failure to queue a call.
constexpr CallHandle kUnknownCallHandle = std::numeric_limits<size_t>::max();

class StreamReadBuffer {
 public:
  // Returns whether we could read appended data from this buffer. It is NOT always !Empty().
  virtual bool ReadyToRead() = 0;

  // Returns true if this buffer is empty.
  virtual bool Empty() = 0;

  // Resets buffer and release allocated memory.
  virtual void Reset() = 0;

  // Returns true if this buffer is full and we cannot anymore append into it.
  virtual bool Full() = 0;

  // Ensures there is some space to append into. Depending on currently used size.
  // Returns iov's that could be used for appending data into to this buffer.
  virtual Result<IoVecs> PrepareAppend() = 0;

  // Extends amount of appended data by len.
  virtual void DataAppended(size_t len) = 0;

  // Returns currently appended data.
  virtual IoVecs AppendedVecs() = 0;

  // Consumes count bytes of appended data. If prepend is not empty, then all future reads should
  // write data to prepend, until it is filled. I.e. unfilled part of prepend will be the first
  // entry of vector returned by PrepareAppend.
  virtual void Consume(size_t count, const Slice& prepend) = 0;

  virtual size_t DataAvailable() = 0;

  // Render this buffer to string.
  virtual std::string ToString() const = 0;

  virtual ~StreamReadBuffer() {}
};

class StreamContext {
 public:
  virtual void UpdateLastActivity() = 0;
  virtual void UpdateLastRead() = 0;
  virtual void UpdateLastWrite() = 0;
  virtual void Transferred(const OutboundDataPtr& data, const Status& status) = 0;
  virtual void Destroy(const Status& status) = 0;

  // Called by underlying stream when stream has been connected (Stream::IsConnected() became true).
  virtual Status Connected() = 0;

  virtual Result<size_t> ProcessReceived(ReadBufferFull read_buffer_full) = 0;
  virtual StreamReadBuffer& ReadBuffer() = 0;

 protected:
  ~StreamContext() {}
};

class Stream {
 public:
  Stream() = default;

  Stream(const Stream&) = delete;
  void operator=(const Stream&) = delete;

  virtual Status Start(bool connect, ev::loop_ref* loop, StreamContext* context) = 0;
  virtual void Close() = 0;
  virtual void Shutdown(const Status& status) = 0;

  // Returns handle to block associated with this data. This handle could be used to cancel
  // transfer of this block using Cancelled, e.g. when a unsent call times out.
  // May return kUnknownCallHandle.
  virtual Result<CallHandle> Send(OutboundDataPtr data) ON_REACTOR_THREAD = 0;

  virtual Status TryWrite() = 0;
  virtual void ParseReceived() = 0;
  virtual size_t GetPendingWriteBytes() = 0;
  virtual bool Cancelled(size_t handle) = 0;

  virtual bool Idle(std::string* reason_not_idle) = 0;
  virtual bool IsConnected() = 0;
  virtual void DumpPB(const DumpRunningRpcsRequestPB& req, RpcConnectionPB* resp) = 0;

  // The address of the remote end of the connection.
  virtual const Endpoint& Remote() const = 0;

  // The address of the local end of the connection.
  virtual const Endpoint& Local() const = 0;

  virtual std::string ToString() const;

  const std::string& LogPrefix() {
    if (log_prefix_.empty()) {
      log_prefix_ = ToString() + ": ";
    }
    return log_prefix_;
  }

  virtual const Protocol* GetProtocol() = 0;

  virtual ~Stream() {}

 protected:
  void ResetLogPrefix() {
    log_prefix_.clear();
  }

  std::string log_prefix_;
};

struct StreamCreateData {
  Endpoint remote;
  const std::string& remote_hostname;
  Socket* socket;
  size_t receive_buffer_size;
  std::shared_ptr<MemTracker> mem_tracker;
  scoped_refptr<MetricEntity> metric_entity;
};

class StreamFactory {
 public:
  virtual std::unique_ptr<Stream> Create(const StreamCreateData& data) = 0;

  virtual ~StreamFactory() {}
};

class Protocol {
 public:
  explicit Protocol(const std::string& id) : id_(id) {}

  Protocol(const Protocol& schema) = delete;
  void operator=(const Protocol& schema) = delete;

  const std::string& ToString() const { return id_; }

  const std::string& id() const { return id_; }

 private:
  std::string id_;
};

} // namespace rpc
} // namespace yb
