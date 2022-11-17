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

#include <string>

#include "yb/client/client_fwd.h"
#include "yb/client/callbacks.h"

#include "yb/yql/redis/redisserver/redis_fwd.h"

namespace yb {
namespace redisserver {

// With regards to the Redis Protocol, kString is Bulk String and kStatus is Simple String
// kIgnore is to be used for assertions, where we do not exactly care about the reply.
YB_DEFINE_ENUM(RedisReplyType, (kString)(kStatus)(kArray)(kError)(kInteger)(kNull)(kIgnore));

// Class stores redis reply.
// Redis reply consists of type and value.
// Value could be int64_t when type is kInteger, or vector of replies when type is kArray.
// In other cases value is string.
class RedisReply {
 public:
  RedisReply();
  RedisReply(RedisReplyType type, std::string string);

  explicit RedisReply(int64_t value);

  explicit RedisReply(std::vector<RedisReply> array);

  std::string ToString() const;

  RedisReplyType get_type() const {
    return type_;
  }

  bool is_null() const {
    return get_type() == RedisReplyType::kNull;
  }

  bool is_array() const {
    return get_type() == RedisReplyType::kArray;
  }

  const std::string& as_string() const {
    return string_;
  }

  const std::string& error() const {
    return string_;
  }

  int64_t as_integer() const {
    return int_;
  }

  static RedisReply Ignored() {
    static RedisReply kIgnoreReply(RedisReplyType::kIgnore, "");
    return kIgnoreReply;
  }

  const std::vector<RedisReply>& as_array() const {
    return array_;
  }

  bool operator==(const RedisReply& rhs) const {
    return type_ == rhs.type_ && string_ == rhs.string_ && int_ == rhs.int_ && array_ == array_;
  }

 private:
  RedisReplyType type_;
  std::string string_;
  int64_t int_ = 0;
  std::vector<RedisReply> array_;
};

typedef std::function<void(const RedisReply&)> RedisCallback;
typedef std::vector<std::string> RedisCommand;

// Simple synchronous redis client.
class RedisClient {
 public:
  explicit RedisClient(const std::string& addr, uint16_t port);
  ~RedisClient();

  // Disconnect.
  void Disconnect();

  // Queue command with appropriate callback.
  void Send(RedisCommand command, RedisCallback callback);

  // Send all queued command and process responses, calling callbacks.
  void Commit();

 private:
  class Impl;

  std::unique_ptr<Impl> impl_;
};

} // namespace redisserver
} // namespace yb
