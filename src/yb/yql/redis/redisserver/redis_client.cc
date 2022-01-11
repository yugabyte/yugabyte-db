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

#include "yb/yql/redis/redisserver/redis_client.h"

#include <hiredis/hiredis.h>

#include "yb/gutil/casts.h"

#include "yb/util/cast.h"
#include "yb/util/format.h"
#include "yb/util/logging.h"
#include "yb/util/scope_exit.h"

namespace yb {
namespace redisserver {

RedisReply::RedisReply() : type_(RedisReplyType::kNull) {}

RedisReply::RedisReply(RedisReplyType type, std::string string)
    : type_(type), string_(std::move(string)) {}

RedisReply::RedisReply(int64_t value) : type_(RedisReplyType::kInteger), int_(value) {}

RedisReply::RedisReply(std::vector<RedisReply> array)
    : type_(RedisReplyType::kArray), array_(std::move(array)) {
}

namespace {

RedisReply CreateReply(redisReply* reply) {
  switch (reply->type) {
    case REDIS_REPLY_STRING:
      return RedisReply(RedisReplyType::kString, std::string(reply->str, reply->len));
    case REDIS_REPLY_ARRAY:
      {
        std::vector<RedisReply> replies;
        replies.reserve(reply->elements);
        for (size_t i = 0; i != reply->elements; ++i) {
          replies.push_back(CreateReply(reply->element[i]));
        }
        return RedisReply(std::move(replies));
      }
    case REDIS_REPLY_INTEGER:
      return RedisReply(reply->integer);
    case REDIS_REPLY_NIL:
      return RedisReply();
    case REDIS_REPLY_STATUS:
      return RedisReply(RedisReplyType::kStatus, std::string(reply->str, reply->len));
    case REDIS_REPLY_ERROR:
      return RedisReply(RedisReplyType::kError, std::string(reply->str, reply->len));
    default:
      RedisReply result(
          RedisReplyType::kError, Format("Unsupported reply type: $0", reply->type));
      LOG(ERROR) << result.ToString();
      return result;
  }
}

} // namespace

std::string RedisReply::ToString() const {
  switch (type_) {
    case RedisReplyType::kArray:
      return Format("{$0, $1}", type_, array_);
    case RedisReplyType::kInteger:
      return Format("{$0, $1}", type_, int_);
    default:
      return Format("{$0, $1}", type_, string_);
  }
}

class RedisClient::Impl {
 public:
  Impl(const std::string& addr, uint16_t port) : context_(redisConnect(addr.c_str(), port)) {
    DCHECK_ONLY_NOTNULL(context_);
  }

  void Disconnect() {
    Free();
  }

  void Send(RedisCommand command, RedisCallback callback) {
    queue_.emplace_back(std::move(command), std::move(callback));
  }

  void Commit() {
    if (!context_) {
      NotifyDisconnected();
      return;
    }
    std::vector<const char*> args;
    std::vector<size_t> arg_lens;
    for (const auto& entry : queue_) {
      args.clear();
      arg_lens.clear();
      for (const auto& word : entry.first) {
        args.push_back(word.c_str());
        arg_lens.push_back(word.size());
      }
      if (args.empty()) {
        continue;
      }
      if (redisAppendCommandArgv(
              context_, narrow_cast<int>(args.size()), args.data(), arg_lens.data()) != REDIS_OK) {
        NotifyDisconnected();
        Free();
        return;
      }
    }

    for (const auto& entry : queue_) {
      redisReply* reply = nullptr;
      auto se = ScopeExit([&reply] {
        if (reply) {
          freeReplyObject(reply);
        }
      });
      if (redisGetReply(context_, pointer_cast<void**>(&reply)) != REDIS_OK) {
        NotifyDisconnected();
        Free();
        return;
      }
      entry.second(CreateReply(reply));
    }
    queue_.clear();
  }

  ~Impl() {
    Free();
  }
 private:
  void Free() {
    if (context_) {
      redisFree(context_);
      context_ = nullptr;
    }
  }

  void NotifyDisconnected() {
    RedisReply reply(RedisReplyType::kError, "Disconnected");
    for (const auto& entry : queue_) {
      entry.second(reply);
    }
    queue_.clear();
  }

  redisContext* context_;
  std::vector<std::pair<RedisCommand, RedisCallback>> queue_;
};

RedisClient::RedisClient(const std::string& addr, uint16_t port)
    : impl_(new Impl(addr.c_str(), port)) {
}

RedisClient::~RedisClient() {}

void RedisClient::Disconnect() {
  impl_->Disconnect();
}

void RedisClient::Send(RedisCommand command, RedisCallback callback) {
  impl_->Send(std::move(command), std::move(callback));
}

void RedisClient::Commit() {
  impl_->Commit();
}

} // namespace redisserver
} // namespace yb
