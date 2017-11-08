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

#include <memory>
#include <string>

#include <boost/algorithm/string.hpp>

#include <boost/optional/optional.hpp>

#include "yb/client/client.h"
#include "yb/client/yb_op.h"

#include "yb/common/redis_protocol.pb.h"

#include "yb/gutil/strings/substitute.h"

#include "yb/yql/redis/redisserver/redis_constants.h"
#include "yb/yql/redis/redisserver/redis_parser.h"

#include "yb/util/split.h"
#include "yb/util/status.h"
#include "yb/util/stol_utils.h"

namespace yb {
namespace redisserver {

using yb::client::YBTable;
using yb::client::YBRedisWriteOp;
using yb::client::YBRedisReadOp;
using std::vector;
using std::shared_ptr;
using std::string;

namespace {

constexpr size_t kMaxNumberOfArgs = 1 << 20;
constexpr size_t kLineEndLength = 2;
constexpr char kPositiveInfinity[] = "+inf";
constexpr char kNegativeInfinity[] = "-inf";


string to_lower_case(Slice slice) {
  return boost::to_lower_copy(slice.ToBuffer());
}

CHECKED_STATUS add_string_subkey(const string& subkey, RedisKeyValuePB* kv_pb) {
  kv_pb->add_subkey()->set_string_subkey(subkey);
  return Status::OK();
}

CHECKED_STATUS add_timestamp_subkey(const string &subkey, RedisKeyValuePB *kv_pb) {
  auto timestamp = util::CheckedStoll(subkey);
  RETURN_NOT_OK(timestamp);
  kv_pb->add_subkey()->set_timestamp_subkey(*timestamp);
  return Status::OK();
}

Result<int64_t> ParseInt64(const Slice& slice, const char* field) {
  auto result = util::CheckedStoll(slice);
  if (!result.ok()) {
    return STATUS_SUBSTITUTE(InvalidArgument,
        "$0 field $1 is not a valid number", field, slice.ToDebugString());
  }
  return *result;
}

Result<int32_t> ParseInt32(const Slice& slice, const char* field) {
  auto val = ParseInt64(slice, field);
  if (!val.ok()) {
    return std::move(val.status());
  }
  if (*val < std::numeric_limits<int32_t>::min() ||
      *val > std::numeric_limits<int32_t>::max()) {
    return STATUS_SUBSTITUTE(InvalidArgument,
        "$0 field $1 is not within valid bounds", field, slice.ToDebugString());
  }
  return static_cast<int32_t>(*val);
}

} // namespace

CHECKED_STATUS ParseSet(YBRedisWriteOp *op, const RedisClientCommand& args) {
  if (args[1].empty()) {
    return STATUS_SUBSTITUTE(InvalidArgument,
        "A SET request must have a non empty key field");
  }
  op->mutable_request()->set_allocated_set_request(new RedisSetRequestPB());
  const auto& key = args[1];
  const auto& value = args[2];
  op->mutable_request()->mutable_key_value()->set_key(key.cdata(), key.size());
  op->mutable_request()->mutable_key_value()->add_value(value.cdata(), value.size());
  op->mutable_request()->mutable_key_value()->set_type(REDIS_TYPE_STRING);
  int idx = 3;
  while (idx < args.size()) {
    if (args[idx] == "EX" || args[idx] == "PX") {
      if (args.size() < idx + 2) {
        return STATUS_SUBSTITUTE(InvalidArgument,
            "Expected TTL field after the EX flag, no value found");
      }
      auto ttl_val = ParseInt64(args[idx + 1], "TTL");
      RETURN_NOT_OK(ttl_val);
      if (*ttl_val < kRedisMinTtlSeconds || *ttl_val > kRedisMaxTtlSeconds) {
        return STATUS_FORMAT(InvalidArgument,
            "TTL field $0 is not within valid bounds", args[idx + 1]);
      }
      const int64_t milliseconds_per_unit =
          args[idx] == "EX" ? MonoTime::kMillisecondsPerSecond : 1;
      op->mutable_request()->mutable_set_request()->set_ttl(*ttl_val * milliseconds_per_unit);
      idx += 2;
    } else if (args[idx] == "XX") {
      op->mutable_request()->mutable_set_request()->set_mode(REDIS_WRITEMODE_UPDATE);
      idx += 1;
    } else if (args[idx] == "NX") {
      op->mutable_request()->mutable_set_request()->set_mode(REDIS_WRITEMODE_INSERT);
      idx += 1;
    } else {
      return STATUS_FORMAT(InvalidArgument,
          "Unidentified argument $0 found while parsing set command", args[idx]);
    }
  }
  return Status::OK();
}

// TODO: support MSET
CHECKED_STATUS ParseMSet(YBRedisWriteOp *op, const RedisClientCommand& args) {
  if (args.size() < 3 || args.size() % 2 == 0) {
    return STATUS_SUBSTITUTE(InvalidArgument,
        "An MSET request must have at least 3, odd number of arguments, found $0", args.size());
  }
  return STATUS(InvalidCommand, "MSET command not yet supported");
}

CHECKED_STATUS ParseHSet(YBRedisWriteOp *op, const RedisClientCommand& args) {
  const auto& key = args[1];
  const auto& subkey = args[2];
  const auto& value = args[3];
  op->mutable_request()->set_allocated_set_request(new RedisSetRequestPB());
  op->mutable_request()->mutable_key_value()->set_key(key.cdata(), key.size());
  op->mutable_request()->mutable_key_value()->set_type(REDIS_TYPE_HASH);
  op->mutable_request()->mutable_key_value()->add_subkey()->set_string_subkey(subkey.cdata(),
                                                                              subkey.size());
  op->mutable_request()->mutable_key_value()->add_value(value.cdata(), value.size());
  return Status::OK();
}

template <typename AddSubKey>
CHECKED_STATUS ParseHMSetLikeCommands(YBRedisWriteOp *op, const RedisClientCommand& args,
                                      const RedisDataType& type,
                                      AddSubKey add_sub_key) {
  if (args.size() < 4 || args.size() % 2 == 1) {
    return STATUS_SUBSTITUTE(InvalidArgument,
                             "wrong number of arguments: $0 for command: $1", args.size(),
                             string(args[0].cdata(), args[0].size()));
  }
  op->mutable_request()->set_allocated_set_request(new RedisSetRequestPB());
  op->mutable_request()->mutable_key_value()->set_type(type);
  op->mutable_request()->mutable_key_value()->set_key(args[1].cdata(), args[1].size());
  // We remove duplicates from the subkeys here.
  std::unordered_map<string, string> kv_map;
  for (int i = 2; i < args.size(); i += 2) {
    // EXPIRE_AT/EXPIRE_IN only supported for redis timeseries currently.
    if ((args[i] == kExpireAt || args[i] == kExpireIn) && type == REDIS_TYPE_TIMESERIES) {
      if (i + 2 != args.size()) {
        return STATUS_SUBSTITUTE(InvalidArgument, "$0 should be at the end of the command",
                                 string(args[i].cdata(), args[i].size()));
      }
      auto temp = util::CheckedStoll(args[i + 1]);
      RETURN_NOT_OK(temp);
      auto ttl = args[i] == kExpireIn
          ? *temp
          : *temp - GetCurrentTimeMicros() / MonoTime::kMicrosecondsPerSecond;

      if (ttl > kRedisMaxTtlSeconds || ttl < kRedisMinTtlSeconds) {
        return STATUS_SUBSTITUTE(InvalidArgument, "TTL: $0 needs be in the range [$1, $2]", ttl,
                                 kRedisMinTtlSeconds, kRedisMaxTtlSeconds);
      }
      // Need to pass ttl in milliseconds, user supplied values are in seconds.
      op->mutable_request()->mutable_set_request()->set_ttl(ttl * MonoTime::kMillisecondsPerSecond);
    } else {
      kv_map[args[i].ToBuffer()] = args[i + 1].ToBuffer();
    }
  }
  for (const auto& kv : kv_map) {
    auto req_kv = op->mutable_request()->mutable_key_value();
    RETURN_NOT_OK(add_sub_key(kv.first, req_kv));
    req_kv->add_value(kv.second);
  }
  return Status::OK();
}

CHECKED_STATUS ParseHMSet(YBRedisWriteOp *op, const RedisClientCommand& args) {
  DCHECK_EQ("hmset", to_lower_case(args[0]))
      << "Parsing hmset request where first arg is not hmset.";
  return ParseHMSetLikeCommands(op, args, REDIS_TYPE_HASH, add_string_subkey);
}

CHECKED_STATUS ParseTsAdd(YBRedisWriteOp *op, const RedisClientCommand& args) {
  DCHECK_EQ("tsadd", to_lower_case(args[0]))
    << "Parsing hmset request where first arg is not hmset.";
  return ParseHMSetLikeCommands(op, args, REDIS_TYPE_TIMESERIES,
                                add_timestamp_subkey);
}


template <typename YBRedisOp, typename AddSubKey>
CHECKED_STATUS ParseCollection(YBRedisOp *op,
                               const RedisClientCommand& args,
                               boost::optional<RedisDataType> type,
                               AddSubKey add_sub_key,
                               bool remove_duplicates = true) {
  const auto& key = args[1];
  op->mutable_request()->mutable_key_value()->set_key(key.cdata(), key.size());
  if (type) {
    op->mutable_request()->mutable_key_value()->set_type(*type);
  }
  if (remove_duplicates) {
    // We remove duplicates from the subkeys here.
    std::set<string> subkey_set;
    for (size_t i = 2; i < args.size(); i++) {
      subkey_set.insert(string(args[i].cdata(), args[i].size()));
    }
    for (const auto &val : subkey_set) {
      RETURN_NOT_OK(add_sub_key(val, op->mutable_request()->mutable_key_value()));
    }
  } else {
    for (size_t i = 2; i < args.size(); i++) {
      RETURN_NOT_OK(add_sub_key(string(args[i].cdata(), args[i].size()),
                                op->mutable_request()->mutable_key_value()));
    }
  }
  return Status::OK();
}

CHECKED_STATUS ParseHDel(YBRedisWriteOp *op, const RedisClientCommand& args) {
  op->mutable_request()->set_allocated_del_request(new RedisDelRequestPB());
  return ParseCollection(op, args, REDIS_TYPE_HASH, add_string_subkey);
}

CHECKED_STATUS ParseTsRem(YBRedisWriteOp *op, const RedisClientCommand& args) {
  op->mutable_request()->set_allocated_del_request(new RedisDelRequestPB());
  return ParseCollection(op, args, REDIS_TYPE_TIMESERIES, add_timestamp_subkey);
}

CHECKED_STATUS ParseSAdd(YBRedisWriteOp *op, const RedisClientCommand& args) {
  op->mutable_request()->set_allocated_add_request(new RedisAddRequestPB());
  return ParseCollection(op, args, REDIS_TYPE_SET, add_string_subkey);
}

CHECKED_STATUS ParseSRem(YBRedisWriteOp *op, const RedisClientCommand& args) {
  op->mutable_request()->set_allocated_del_request(new RedisDelRequestPB());
  return ParseCollection(op, args, REDIS_TYPE_SET, add_string_subkey);
}

CHECKED_STATUS ParseGetSet(YBRedisWriteOp *op, const RedisClientCommand& args) {
  const auto& key = args[1];
  const auto& value = args[2];
  op->mutable_request()->set_allocated_getset_request(new RedisGetSetRequestPB());
  op->mutable_request()->mutable_key_value()->set_key(key.cdata(), key.size());
  op->mutable_request()->mutable_key_value()->add_value(value.cdata(), value.size());
  return Status::OK();
}

CHECKED_STATUS ParseAppend(YBRedisWriteOp* op, const RedisClientCommand& args) {
  const auto& key = args[1];
  const auto& value = args[2];
  op->mutable_request()->set_allocated_append_request(new RedisAppendRequestPB());
  op->mutable_request()->mutable_key_value()->set_key(key.cdata(), key.size());
  op->mutable_request()->mutable_key_value()->add_value(value.cdata(), value.size());
  return Status::OK();
}

// Note: deleting only one key is supported using one command as of now.
CHECKED_STATUS ParseDel(YBRedisWriteOp* op, const RedisClientCommand& args) {
  const auto& key = args[1];
  op->mutable_request()->set_allocated_del_request(new RedisDelRequestPB());
  op->mutable_request()->mutable_key_value()->set_key(key.cdata(), key.size());
  // We should be able to delete all types of top level keys
  op->mutable_request()->mutable_key_value()->set_type(REDIS_TYPE_NONE);
  return Status::OK();
}

CHECKED_STATUS ParseSetRange(YBRedisWriteOp* op, const RedisClientCommand& args) {
  const auto& key = args[1];
  const auto& value = args[3];
  op->mutable_request()->set_allocated_set_range_request(new RedisSetRangeRequestPB());
  op->mutable_request()->mutable_key_value()->set_key(key.cdata(), key.size());
  op->mutable_request()->mutable_key_value()->add_value(value.cdata(), value.size());

  auto offset = ParseInt32(args[2], "offset");
  RETURN_NOT_OK(offset);
  op->mutable_request()->mutable_set_range_request()->set_offset(*offset);

  return Status::OK();
}

CHECKED_STATUS ParseIncr(YBRedisWriteOp* op, const RedisClientCommand& args) {
  const auto& key = args[1];
  op->mutable_request()->set_allocated_incr_request(new RedisIncrRequestPB());
  op->mutable_request()->mutable_key_value()->set_key(key.cdata(), key.size());
  return Status::OK();
}

CHECKED_STATUS ParseGet(YBRedisReadOp* op, const RedisClientCommand& args) {
  op->mutable_request()->set_allocated_get_request(new RedisGetRequestPB());
  const auto& key = args[1];
  if (key.empty()) {
    return STATUS_SUBSTITUTE(InvalidArgument,
        "A GET request must have non empty key field");
  }
  op->mutable_request()->mutable_key_value()->set_key(key.cdata(), key.size());
  op->mutable_request()->mutable_get_request()->set_request_type(
      RedisGetRequestPB_GetRequestType_GET);
  return Status::OK();
}

//  Used for HGET/HSTRLEN/HEXISTS. Also for HMGet
//  CMD <KEY> [<SUB-KEY>]*
CHECKED_STATUS ParseHGetLikeCommands(YBRedisReadOp* op, const RedisClientCommand& args,
                                     RedisGetRequestPB_GetRequestType request_type,
                                     bool remove_duplicates = false) {
  op->mutable_request()->set_allocated_get_request(new RedisGetRequestPB());
  op->mutable_request()->mutable_get_request()->set_request_type(request_type);

  return ParseCollection(op, args, boost::none, add_string_subkey, remove_duplicates);
}

// TODO: Support MGET
CHECKED_STATUS ParseMGet(YBRedisReadOp* op, const RedisClientCommand& args) {
  return STATUS(InvalidCommand, "MGET command not yet supported");
}

CHECKED_STATUS ParseHGet(YBRedisReadOp* op, const RedisClientCommand& args) {
  return ParseHGetLikeCommands(op, args, RedisGetRequestPB_GetRequestType_HGET);
}

CHECKED_STATUS ParseTsBoundArg(const Slice& slice, RedisSubKeyBoundPB* bound_pb, bool exclusive) {
  string bound(slice.cdata(), slice.size());
  if (bound == kPositiveInfinity) {
    bound_pb->set_infinity_type(RedisSubKeyBoundPB_InfinityType_POSITIVE);
  } else if (bound == kNegativeInfinity) {
    bound_pb->set_infinity_type(RedisSubKeyBoundPB_InfinityType_NEGATIVE);
  } else {
    auto ts_bound = util::CheckedStoll(slice);
    RETURN_NOT_OK(ts_bound);
    bound_pb->set_is_exclusive(exclusive);
    bound_pb->mutable_subkey_bound()->set_timestamp_subkey(*ts_bound);
  }
  return Status::OK();
}

CHECKED_STATUS ParseTsSubKeyBound(const Slice& slice, RedisSubKeyBoundPB* bound_pb) {
  if (slice.empty()) {
    return STATUS(InvalidArgument, "range bound key cannot be empty");
  }

  if (slice[0] == '(' && slice.size() > 1) {
    auto slice_copy = slice;
    slice_copy.remove_prefix(1);
    RETURN_NOT_OK(ParseTsBoundArg(slice_copy, bound_pb, /* exclusive */ true));
  } else {
    RETURN_NOT_OK(ParseTsBoundArg(slice, bound_pb, /* exclusive */ false));
  }
  return Status::OK();
}

CHECKED_STATUS ParseTsRangeByTime(YBRedisReadOp* op, const RedisClientCommand& args) {
  op->mutable_request()->set_allocated_get_collection_range_request(
      new RedisCollectionGetRangeRequestPB());
  op->mutable_request()->mutable_get_collection_range_request()->set_request_type(
      RedisCollectionGetRangeRequestPB_GetRangeRequestType_TSRANGEBYTIME);

  const auto& key = args[1];
  RETURN_NOT_OK(ParseTsSubKeyBound(
      args[2],
      op->mutable_request()->mutable_subkey_range()->mutable_lower_bound()));
  RETURN_NOT_OK(ParseTsSubKeyBound(
      args[3],
      op->mutable_request()->mutable_subkey_range()->mutable_upper_bound()));

  op->mutable_request()->mutable_key_value()->set_key(key.ToBuffer());
  return Status::OK();
}

CHECKED_STATUS ParseTsGet(YBRedisReadOp* op, const RedisClientCommand& args) {
  op->mutable_request()->set_allocated_get_request(new RedisGetRequestPB());
  op->mutable_request()->mutable_get_request()->set_request_type(
      RedisGetRequestPB_GetRequestType_TSGET);

  const auto& key = args[1];
  op->mutable_request()->mutable_key_value()->set_key(key.cdata(), key.size());
  auto timestamp = util::CheckedStoll(args[2]);
  RETURN_NOT_OK(timestamp);
  op->mutable_request()->mutable_key_value()->add_subkey()->set_timestamp_subkey(*timestamp);

  return Status::OK();
}

CHECKED_STATUS ParseHStrLen(YBRedisReadOp* op, const RedisClientCommand& args) {
  return ParseHGetLikeCommands(op, args, RedisGetRequestPB_GetRequestType_HSTRLEN);
}

CHECKED_STATUS ParseHExists(YBRedisReadOp* op, const RedisClientCommand& args) {
  return ParseHGetLikeCommands(op, args, RedisGetRequestPB_GetRequestType_HEXISTS);
}

CHECKED_STATUS ParseHMGet(YBRedisReadOp* op, const RedisClientCommand& args) {
  return ParseHGetLikeCommands(op, args, RedisGetRequestPB_GetRequestType_HMGET);
}

CHECKED_STATUS ParseHGetAll(YBRedisReadOp* op, const RedisClientCommand& args) {
  return ParseHGetLikeCommands(op, args, RedisGetRequestPB_GetRequestType_HGETALL);
}

CHECKED_STATUS ParseHKeys(YBRedisReadOp* op, const RedisClientCommand& args) {
  return ParseHGetLikeCommands(op, args, RedisGetRequestPB_GetRequestType_HKEYS);
}

CHECKED_STATUS ParseHVals(YBRedisReadOp* op, const RedisClientCommand& args) {
  return ParseHGetLikeCommands(op, args, RedisGetRequestPB_GetRequestType_HVALS);
}

CHECKED_STATUS ParseHLen(YBRedisReadOp* op, const RedisClientCommand& args) {
  return ParseHGetLikeCommands(op, args, RedisGetRequestPB_GetRequestType_HLEN);
}

CHECKED_STATUS ParseSMembers(YBRedisReadOp* op, const RedisClientCommand& args) {
  return ParseHGetLikeCommands(op, args, RedisGetRequestPB_GetRequestType_SMEMBERS);
}

CHECKED_STATUS ParseSIsMember(YBRedisReadOp* op, const RedisClientCommand& args) {
  return ParseHGetLikeCommands(op, args, RedisGetRequestPB_GetRequestType_SISMEMBER);
}

CHECKED_STATUS ParseSCard(YBRedisReadOp* op, const RedisClientCommand& args) {
  return ParseHGetLikeCommands(op, args, RedisGetRequestPB_GetRequestType_SCARD);
}

CHECKED_STATUS ParseStrLen(YBRedisReadOp* op, const RedisClientCommand& args) {
  op->mutable_request()->set_allocated_strlen_request(new RedisStrLenRequestPB());
  const auto& key = args[1];
  op->mutable_request()->mutable_key_value()->set_key(key.cdata(), key.size());
  return Status::OK();
}

// Note: Checking existence of only one key is supported as of now.
CHECKED_STATUS ParseExists(YBRedisReadOp* op, const RedisClientCommand& args) {
  op->mutable_request()->set_allocated_exists_request(new RedisExistsRequestPB());
  const auto& key = args[1];
  op->mutable_request()->mutable_key_value()->set_key(key.cdata(), key.size());
  return Status::OK();
}

CHECKED_STATUS ParseGetRange(YBRedisReadOp* op, const RedisClientCommand& args) {
  op->mutable_request()->set_allocated_get_range_request(new RedisGetRangeRequestPB());
  const auto& key = args[1];
  op->mutable_request()->mutable_key_value()->set_key(key.cdata(), key.size());

  auto start = ParseInt32(args[2], "Start");
  RETURN_NOT_OK(start);
  op->mutable_request()->mutable_get_range_request()->set_start(*start);

  auto end = ParseInt32(args[3], "End");
  RETURN_NOT_OK(end);
  op->mutable_request()->mutable_get_range_request()->set_end(*end);

  return Status::OK();
}

// Begin of input is going to be consumed, so we should adjust our pointers.
// Since the beginning of input is being consumed by shifting the remaining bytes to the
// beginning of the buffer.
void RedisParser::Consume(size_t count) {
  pos_ -= count;
  if (token_begin_ != nullptr) {
    token_begin_ -= count;
  }
}

// New data arrived, so update the end of available bytes.
void RedisParser::Update(Slice source) {
  ptrdiff_t delta = source.data() - begin_;
  begin_ = source.data();
  end_ = source.end();
  pos_ += delta;
  if (token_begin_ != nullptr) {
    token_begin_ += delta;
  }

  DCHECK_LE(pos_, end_);
}

// Parse next command.
CHECKED_STATUS RedisParser::NextCommand(const uint8_t** end_of_command) {
  *end_of_command = nullptr;
  while (pos_ != end_) {
    incomplete_ = false;
    Status status = AdvanceToNextToken();
    if (!status.ok()) {
      return status;
    }
    if (incomplete_) {
      pos_ = end_;
      *end_of_command = nullptr;
      return Status::OK();
    }
    if (state_ == State::FINISHED) {
      *end_of_command = pos_;
      state_ = State::INITIAL;
      return Status::OK();
    }
  }
  return Status::OK();
}

CHECKED_STATUS RedisParser::AdvanceToNextToken() {
  switch (state_) {
    case State::INITIAL:
      return Initial();
    case State::SINGLE_LINE:
      return SingleLine();
    case State::BULK_HEADER:
      return BulkHeader();
    case State::BULK_ARGUMENT_SIZE:
      return BulkArgumentSize();
    case State::BULK_ARGUMENT_BODY:
      return BulkArgumentBody();
    case State::FINISHED:
      return STATUS(IllegalState, "Should not be in FINISHED state during NextToken");
  }
  LOG(FATAL) << "Unexpected parser state: " << util::to_underlying(state_);
}

CHECKED_STATUS RedisParser::Initial() {
  token_begin_ = pos_;
  state_ = *pos_ == '*' ? State::BULK_HEADER : State::SINGLE_LINE;
  return Status::OK();
}

CHECKED_STATUS RedisParser::SingleLine() {
  auto status = FindEndOfLine();
  if (!status.ok() || incomplete_) {
    return status;
  }
  auto start = token_begin_;
  auto finish = pos_ - 2;
  while (start < finish && isspace(*start)) {
    ++start;
  }
  if (start >= finish) {
    return STATUS(InvalidArgument, "Empty line");
  }
  if (args_) {
    RETURN_NOT_OK(util::SplitArgs(Slice(start, finish), args_));
  }
  state_ = State::FINISHED;
  return Status::OK();
}

CHECKED_STATUS RedisParser::BulkHeader() {
  auto status = FindEndOfLine();
  if (!status.ok() || incomplete_) {
    return status;
  }
  ptrdiff_t num_args = 0;
  RETURN_NOT_OK(ParseNumber('*', 1, kMaxNumberOfArgs, "Number of lines in multiline", &num_args));
  if (args_) {
    args_->clear();
    args_->reserve(num_args);
  }
  state_ = State::BULK_ARGUMENT_SIZE;
  token_begin_ = pos_;
  arguments_left_ = num_args;
  return Status::OK();
}

CHECKED_STATUS RedisParser::BulkArgumentSize() {
  auto status = FindEndOfLine();
  if (!status.ok() || incomplete_) {
    return status;
  }
  ptrdiff_t current_size = 0;
  RETURN_NOT_OK(ParseNumber('$', 0, kMaxBufferSize, "Argument size", &current_size));
  state_ = State::BULK_ARGUMENT_BODY;
  token_begin_ = pos_;
  current_argument_size_ = current_size;
  return Status::OK();
}

CHECKED_STATUS RedisParser::BulkArgumentBody() {
  auto desired_position = token_begin_ + current_argument_size_ + kLineEndLength;
  if (desired_position > end_) {
    incomplete_ = true;
    pos_ = end_;
    return Status::OK();
  }
  if (desired_position[-1] != '\n' || desired_position[-2] != '\r') {
    return STATUS(NetworkError, "No \\r\\n after bulk");
  }
  if (args_) {
    args_->emplace_back(token_begin_, current_argument_size_);
  }
  --arguments_left_;
  pos_ = desired_position;
  token_begin_ = pos_;
  if (arguments_left_ == 0) {
    state_ = State::FINISHED;
  } else {
    state_ = State::BULK_ARGUMENT_SIZE;
  }
  return Status::OK();
}

CHECKED_STATUS RedisParser::FindEndOfLine() {
  auto new_line = static_cast<const uint8_t *>(memchr(pos_, '\n', end_ - pos_));
  incomplete_ = new_line == nullptr;
  if (!incomplete_) {
    if (new_line == token_begin_) {
      return STATUS(NetworkError, "End of line at the beginning of a Redis command");
    }
    if (new_line[-1] != '\r') {
      return STATUS(NetworkError, "\\n is not prefixed with \\r");
    }
    pos_ = ++new_line;
  }
  return Status::OK();
}

// Parses number with specified bounds.
// Number is located in separate line, and contain prefix before actual number.
// Line starts at token_begin_ and pos_ is a start of next line.
CHECKED_STATUS RedisParser::ParseNumber(char prefix,
                                        ptrdiff_t min,
                                        ptrdiff_t max,
                                        const char* name,
                                        ptrdiff_t* out) {
  if (*token_begin_ != prefix) {
    return STATUS_SUBSTITUTE(Corruption,
                             "Invalid character before number, expected: $0, but found: $1",
                             prefix,
                             static_cast<char>(*token_begin_));
  }
  auto number_begin = token_begin_ + 1;
  auto expected_stop = pos_ - kLineEndLength;
  auto parsed_number = util::CheckedStoll(Slice(number_begin, expected_stop));
  RETURN_NOT_OK(parsed_number);
  static_assert(sizeof(*parsed_number) == sizeof(*out), "Expected size");
  SCHECK_BOUNDS(*parsed_number,
                min,
                max,
                Corruption,
                yb::Format("$0 out of expected range [$1, $2] : $3",
                           name, min, max, *parsed_number));
  *out = static_cast<ptrdiff_t>(*parsed_number);
  return Status::OK();
}

}  // namespace redisserver
}  // namespace yb
