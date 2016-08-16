// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.

#ifndef YB_RPC_TRANSFER_H_
#define YB_RPC_TRANSFER_H_

#include <boost/intrusive/list.hpp>
#include <boost/function.hpp>
#include <boost/utility.hpp>
#include <gflags/gflags.h>
#include <stdint.h>
#include <string>
#include <vector>

#include "redis/src/sds.h"

#include "yb/rpc/rpc_header.pb.h"
#include "yb/util/net/sockaddr.h"
#include "yb/util/status.h"
#include "yb/rpc/constants.h"

DECLARE_int32(rpc_max_message_size);

namespace google {
namespace protobuf {
class Message;
}  // namespace protobuf
}  // namespace google

namespace yb {

class Socket;

namespace rpc {

class Messenger;
struct TransferCallbacks;

struct RedisClientCommand {
  std::vector<Slice> cmd_args;  // Arguments of current command.
  int num_multi_bulk_args_left = 0;  // Number of multi bulk arguments left to read.
  int64_t current_multi_bulk_arg_len = -1;  // Length of bulk argument in multi bulk request.
};

// This class is used internally by the RPC layer to represent an inbound
// transfer in progress.
//
// Inbound Transfer objects are created by a Connection receiving data. When the
// message is fully received, it is either parsed as a call, or a call response,
// and the InboundTransfer object itself is handed off.
class AbstractInboundTransfer {
 public:
  virtual ~AbstractInboundTransfer() {}

  // Read from the socket into our buffer.
  virtual Status ReceiveBuffer(Socket& socket) = 0;

  // Return true if any bytes have yet been received.
  virtual bool TransferStarted() const = 0;

  // Return true if the entire transfer has been received.
  virtual bool TransferFinished() const = 0;

  virtual Slice data() const {
    return Slice(buf_);
  }

  // Return a string indicating the status of this transfer (number of bytes received, etc)
  // suitable for logging.
  virtual std::string StatusAsString() const = 0;

 protected:
  faststring buf_;
  int32_t cur_offset_ = 0;  // Index into buf_ where the next byte read from the client is stored.
};

class YBInboundTransfer : public AbstractInboundTransfer {
 public:
  YBInboundTransfer();

  // Read from the socket into our buffer.
  Status ReceiveBuffer(Socket& socket) override;

  // Return true if any bytes have yet been received.
  bool TransferStarted() const override {
    return cur_offset_ != 0;
  }

  // Return true if the entire transfer has been received.
  bool TransferFinished() const override {
    return cur_offset_ == total_length_;
  }

  // Return a string indicating the status of this transfer (number of bytes received, etc)
  // suitable for logging.
  std::string StatusAsString() const override;

 private:
  Status ProcessInboundHeader();

  int32_t total_length_ = kMsgLengthPrefixLength;

  DISALLOW_COPY_AND_ASSIGN(YBInboundTransfer);
};

class RedisInboundTransfer : public AbstractInboundTransfer {
 public:
  RedisInboundTransfer();

  ~RedisInboundTransfer();

  // Read from the socket into our buffer.
  Status ReceiveBuffer(Socket& socket) override;

  bool TransferStarted() const override {
    return cur_offset_ != 0;
  }

  bool TransferFinished() const override {
    return done_;
  }

  // Return a string indicating the status of this transfer (number of bytes received, etc.)
  // suitable for logging.
  std::string StatusAsString() const override;

  const RedisClientCommand &client_command() const {
    return client_command_;
  }

 private:
  static constexpr int kProtoIOBufLen = 1024 * 16;  // I/O buffer size for reading client commands.

  void CheckReadCompletely();

  // To be called only for client commands in the multi format.
  // e.g:
  //     "*3\r\n$3\r\nset\r\n$3\r\nfoo\r\n$4\r\nTEST\r\n"
  // returns true if the whole command has been read, and is ready for processing.
  // returns false if more data needs to be read.
  bool CheckMultiBulkBuffer();

  // To be called only for client commands in the inline format.
  // e.g:
  //     "set foo TEST\r\n"
  // returns true if the whole command has been read, and is ready for processing.
  // returns false if more data needs to be read.
  //
  // Deviation from Redis's implementation in networking.c
  // 1) This does not translate escaped characters, and
  // 2) We don't handle the case where the delimiter only has '\n' instead of '\r\n'
  //
  // We haven't seen a place where this is used by the redis clients. All known places seem to use
  // CheckMultiBulkBuffer(). If we find places where this is used, we can come back and make this
  // compliant. If we are sure this doesn't get used, perhaps, we can get rid of this altogether.
  bool CheckInlineBuffer();

  // Returns true if the buffer has the \r\n required at the end of the token.
  bool FindEndOfLine();
  int64_t ParseNumber();

  int64_t cur_offset_ = 0;  // index into buf_ where the next byte read from the client is stored.
  RedisClientCommand client_command_;
  bool done_ = false;
  int64_t parsing_pos_ = 0;  // index into buf_, from which the input needs to be parsed.
  int64_t searching_pos_ = 0;  // index into buf_, from which the input needs to be searched.
                               // Typically ends up being equal to parsing_pos_ except when we
                               // receive partial data. We don't want to search the searched data
                               // again. Otherwise, the worst case analysis takes us to O(N^2) where
                               // N is the message length.

  DISALLOW_COPY_AND_ASSIGN(RedisInboundTransfer);
};


// When the connection wants to send data, it creates an OutboundTransfer object
// to encompass it. This sits on a queue within the Connection, so that each time
// the Connection wakes up with a writable socket, it consumes more bytes off
// the next pending transfer in the queue.
//
// Upon completion of the transfer, a callback is triggered.
class OutboundTransfer : public boost::intrusive::list_base_hook<> {
 public:
  enum { kMaxPayloadSlices = 10 };

  // Create a new transfer. The 'payload' slices will be concatenated and
  // written to the socket. When the transfer completes or errors, the
  // appropriate method of 'callbacks' is invoked.
  //
  // Does not take ownership of the callbacks object or the underlying
  // memory of the slices. The slices must remain valid until the callback
  // is triggered.
  //
  // NOTE: 'payload' is currently restricted to a maximum of kMaxPayloadSlices
  // slices.
  OutboundTransfer(const std::vector<Slice> &payload,
                   TransferCallbacks *callbacks);

  // Destruct the transfer. A transfer object should never be deallocated
  // before it has either (a) finished transferring, or (b) been Abort()ed.
  ~OutboundTransfer();

  // Abort the current transfer, with the given status.
  // This triggers TransferCallbacks::NotifyTransferAborted.
  void Abort(const Status &status);

  // send from our buffers into the sock
  Status SendBuffer(Socket &socket);

  // Return true if any bytes have yet been sent.
  bool TransferStarted() const {
    return cur_offset_in_slice_ != 0 || cur_slice_idx_ != 0;
  }

  // Return true if the entire transfer has been sent.
  bool TransferFinished() const {
    if (cur_slice_idx_ == n_payload_slices_) {
      DCHECK_EQ(0, cur_offset_in_slice_);  // sanity check
      return true;
    }
    return false;
  }

  // Return the total number of bytes to be sent (including those already sent)
  int32_t TotalLength() const;

  std::string HexDump() const;

 private:
  // Slices to send. Uses an array here instead of a vector to avoid an expensive
  // vector construction (improved performance a couple percent).
  Slice payload_slices_[kMaxPayloadSlices];
  size_t n_payload_slices_;

  // The current slice that is being sent.
  int32_t cur_slice_idx_;
  // The number of bytes in the above slice which has already been sent.
  int32_t cur_offset_in_slice_;

  TransferCallbacks *callbacks_;

  bool aborted_;

  DISALLOW_COPY_AND_ASSIGN(OutboundTransfer);
};

// Callbacks made after a transfer completes.
struct TransferCallbacks {
 public:
  virtual ~TransferCallbacks();

  // The transfer finished successfully.
  virtual void NotifyTransferFinished() = 0;

  // The transfer was aborted (e.g because the connection died or an error occurred).
  virtual void NotifyTransferAborted(const Status &status) = 0;
};

}  // namespace rpc
}  // namespace yb
#endif  // YB_RPC_TRANSFER_H_
