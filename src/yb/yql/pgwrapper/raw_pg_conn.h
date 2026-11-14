// Copyright (c) YugabyteDB, Inc.
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

#include <chrono>
#include <string>
#include <vector>

#include "yb/gutil/casts.h"
#include "yb/util/cast.h"
#include "yb/util/monotime.h"
#include "yb/util/net/net_util.h"
#include "yb/util/net/sockaddr.h"
#include "yb/util/net/socket.h"
#include "yb/util/result.h"
#include "yb/util/status_format.h"
#include "yb/util/tsan_util.h"

namespace yb::pgwrapper {

using namespace std::chrono_literals;

// Minimal wire-protocol client for what libpq cannot send: an Execute with a row limit, which
// suspends the portal so later Execute+Sync cycles run without a Bind; and message orders such as a
// Parse between a Bind and its Execute, or a Close of a statement, sent as one batch before Sync.
class RawPgConn {
 public:
  struct Message {
    char type;
    std::string body;
  };
  Status Connect(const HostPort& host_port, const std::string& user, const std::string& db) {
    auto endpoint = VERIFY_RESULT(ParseEndpoint(host_port.ToString(), 0));
    RETURN_NOT_OK(socket_.Init(0));
    RETURN_NOT_OK(socket_.Connect(endpoint));
    std::string body;
    AppendInt32(&body, 3 << 16);  // protocol 3.0
    AppendCString(&body, "user");
    AppendCString(&body, user);
    AppendCString(&body, "database");
    AppendCString(&body, db);
    body.push_back(0);
    RETURN_NOT_OK(Send(0, body));
    return ReadUntilReadyForQuery();
  }

  Status SimpleQuery(const std::string& sql) {
    std::string body;
    AppendCString(&body, sql);
    RETURN_NOT_OK(Send('Q', body));
    return ReadUntilReadyForQuery();
  }

  Status Parse(const std::string& stmt, const std::string& query) {
    std::string body;
    AppendCString(&body, stmt);
    AppendCString(&body, query);
    AppendInt16(&body, 0);  // no parameter types
    RETURN_NOT_OK(Send('P', body));
    RETURN_NOT_OK(Send('S', ""));
    return ReadUntilReadyForQuery();
  }

  Status BindExecute(const std::string& portal, const std::string& stmt, int32_t max_rows) {
    std::string body;
    AppendCString(&body, portal);
    AppendCString(&body, stmt);
    AppendInt16(&body, 0);  // no parameter format codes
    AppendInt16(&body, 0);  // no parameters
    AppendInt16(&body, 0);  // no result format codes
    RETURN_NOT_OK(Send('B', body));
    return Execute(portal, max_rows);
  }

  Status Execute(const std::string& portal, int32_t max_rows) {
    RETURN_NOT_OK(SendExecute(portal, max_rows));
    RETURN_NOT_OK(SendSync());
    return ReadUntilReadyForQuery();
  }

  // The Send* calls below queue one message each and read nothing; SendSync ends the batch and
  // ReadMessages returns every reply up to ReadyForQuery.
  Status SendParse(const std::string& stmt, const std::string& query) {
    std::string body;
    AppendCString(&body, stmt);
    AppendCString(&body, query);
    AppendInt16(&body, 0);  // no parameter types
    return Send('P', body);
  }

  Status SendBind(const std::string& portal, const std::string& stmt) {
    std::string body;
    AppendCString(&body, portal);
    AppendCString(&body, stmt);
    AppendInt16(&body, 0);  // no parameter format codes
    AppendInt16(&body, 0);  // no parameters
    AppendInt16(&body, 0);  // no result format codes
    return Send('B', body);
  }

  Status SendExecute(const std::string& portal, int32_t max_rows) {
    std::string body;
    AppendCString(&body, portal);
    AppendInt32(&body, max_rows);
    return Send('E', body);
  }

  // type is 'S' for a statement, 'P' for a portal.
  Status SendClose(char type, const std::string& name) {
    std::string body;
    body.push_back(type);
    AppendCString(&body, name);
    return Send('C', body);
  }

  Status SendSync() { return Send('S', ""); }

  Status SendSimpleQuery(const std::string& sql) {
    std::string body;
    AppendCString(&body, sql);
    return Send('Q', body);
  }

  Result<std::vector<Message>> ReadMessages() {
    std::vector<Message> messages;
    for (;;) {
      auto message = VERIFY_RESULT(ReadMessage());
      const auto type = message.type;
      messages.push_back(std::move(message));
      if (type == 'Z') {
        return messages;
      }
    }
  }

  // The first column of each DataRow, as text.
  static std::vector<std::string> FirstColumns(const std::vector<Message>& messages) {
    std::vector<std::string> values;
    for (const auto& message : messages) {
      if (message.type != 'D') {
        continue;
      }
      const auto* data = pointer_cast<const uint8_t*>(message.body.data());
      const auto len = ReadInt32(data + 2);  // after the int16 column count
      values.emplace_back(message.body.data() + 6, len < 0 ? 0 : len);
    }
    return values;
  }

  // The message (M field) of each ErrorResponse.
  static std::vector<std::string> Errors(const std::vector<Message>& messages) {
    std::vector<std::string> errors;
    for (const auto& message : messages) {
      if (message.type != 'E') {
        continue;
      }
      for (size_t pos = 0; pos < message.body.size() && message.body[pos];) {
        const auto end = message.body.find('\0', pos);
        if (message.body[pos] == 'M') {
          errors.push_back(message.body.substr(pos + 1, end - pos - 1));
        }
        pos = end + 1;
      }
    }
    return errors;
  }

 private:
  static void AppendInt32(std::string* out, int32_t value) {
    for (int shift = 24; shift >= 0; shift -= 8) {
      out->push_back(static_cast<char>((value >> shift) & 0xff));
    }
  }

  static void AppendInt16(std::string* out, int16_t value) {
    out->push_back(static_cast<char>((value >> 8) & 0xff));
    out->push_back(static_cast<char>(value & 0xff));
  }

  static void AppendCString(std::string* out, const std::string& value) {
    out->append(value);
    out->push_back(0);
  }

  static int32_t ReadInt32(const uint8_t* buf) {
    return (buf[0] << 24) | (buf[1] << 16) | (buf[2] << 8) | buf[3];
  }

  MonoTime Deadline() const { return MonoTime::NowPlus(30s * kTimeMultiplier); }

  // type 0 is the startup message, which has no type byte.
  Status Send(char type, const std::string& body) {
    std::string msg;
    if (type) {
      msg.push_back(type);
    }
    AppendInt32(&msg, narrow_cast<int32_t>(body.size() + 4));
    msg += body;
    return socket_.BlockingWrite(
        pointer_cast<const uint8_t*>(msg.data()), msg.size(), Deadline());
  }

  Result<Message> ReadMessage() {
    uint8_t header[5];
    RETURN_NOT_OK(socket_.BlockingRecv(header, sizeof(header), Deadline()));
    Message message{.type = static_cast<char>(header[0]),
                    .body = std::string(ReadInt32(header + 1) - 4, 0)};
    if (!message.body.empty()) {
      RETURN_NOT_OK(socket_.BlockingRecv(
          pointer_cast<uint8_t*>(message.body.data()), message.body.size(), Deadline()));
    }
    return message;
  }

  Status ReadUntilReadyForQuery() {
    for (;;) {
      const auto message = VERIFY_RESULT(ReadMessage());
      const auto& body = message.body;
      switch (message.type) {
        case 'Z':
          return Status::OK();
        case 'E':
          return STATUS_FORMAT(QLError, "Server error: $0", body);
        case 'R':
          if (ReadInt32(pointer_cast<const uint8_t*>(body.data())) != 0) {
            return STATUS(NotSupported, "Only trust authentication is supported");
          }
          break;
        default:
          break;
      }
    }
  }

  Socket socket_;
};

} // namespace yb::pgwrapper
