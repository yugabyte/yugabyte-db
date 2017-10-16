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

#include "yb/redisserver/redis_constants.h"
#include "yb/redisserver/redis_parser.h"

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

constexpr int64_t kMaxTTLSec = std::numeric_limits<int64_t>::max() / 1000000;
constexpr size_t kMaxNumberOfArgs = 1 << 20;
constexpr size_t kLineEndLength = 2;


string to_lower_case(Slice slice) {
  return boost::to_lower_copy(slice.ToBuffer());
}

Result<int64_t> ParseInt64(const Slice& slice, const char* field) {
  int64_t val;
  Status s = util::CheckedStoll(slice, &val);
  if (!s.ok()) {
    return STATUS_SUBSTITUTE(InvalidArgument,
        "$0 field $1 is not parsable as a valid number", field, slice.ToDebugString());
  }
  return val;
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

Status ParseSet(YBRedisWriteOp *op, const RedisClientCommand& args) {
  if (args[1].empty()) {
    return STATUS_SUBSTITUTE(InvalidArgument,
        "A SET request must have a non empty key field");
  }
  op->mutable_request()->set_allocated_set_request(new RedisSetRequestPB());
  const auto& key = args[1];
  const auto& value = args[2];
  op->mutable_request()->mutable_key_value()->set_key(key.cdata(), key.size());
  RETURN_NOT_OK(op->SetKey(key));
  op->mutable_request()->mutable_key_value()->add_value(value.cdata(), value.size());
  int idx = 3;
  while (idx < args.size()) {
    if (args[idx] == "EX" || args[idx] == "PX") {
      if (args.size() < idx + 2) {
        return STATUS_SUBSTITUTE(InvalidArgument,
            "Expected TTL field after the EX flag, no value found");
      }
      auto ttl_val = ParseInt64(args[idx + 1], "TTL");
      RETURN_NOT_OK(ttl_val);
      if (*ttl_val <= 0 || *ttl_val > kMaxTTLSec) {
        return STATUS_FORMAT(InvalidArgument,
            "TTL field $0 is not within valid bounds", args[idx + 1]);
      }
      const int64_t milliseconds_per_unit = args[idx] == "EX" ? 1000 : 1;
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
Status ParseMSet(YBRedisWriteOp *op, const RedisClientCommand& args) {
  if (args.size() < 3 || args.size() % 2 == 0) {
    return STATUS_SUBSTITUTE(InvalidArgument,
        "An MSET request must have at least 3, odd number of arguments, found $0", args.size());
  }
  return STATUS(InvalidCommand, "MSET command not yet supported");
}

Status ParseHSet(YBRedisWriteOp *op, const RedisClientCommand& args) {
  const auto& key = args[1];
  const auto& subkey = args[2];
  const auto& value = args[3];
  RETURN_NOT_OK(op->SetKey(key));
  op->mutable_request()->set_allocated_set_request(new RedisSetRequestPB());
  op->mutable_request()->mutable_key_value()->set_key(key.cdata(), key.size());
  op->mutable_request()->mutable_key_value()->set_type(REDIS_TYPE_HASH);
  op->mutable_request()->mutable_key_value()->add_subkey()->set_string_subkey(subkey.cdata(),
                                                                              subkey.size());
  op->mutable_request()->mutable_key_value()->add_value(value.cdata(), value.size());
  return Status::OK();
}

Status ParseTsAdd(YBRedisWriteOp *op, const RedisClientCommand& args) {
  const auto& key = args[1];
  int64_t timestamp;
  RETURN_NOT_OK(util::CheckedStoll(args[2], &timestamp));
  const auto& value = args[3];
  RETURN_NOT_OK(op->SetKey(key));
  op->mutable_request()->set_allocated_set_request(new RedisSetRequestPB());
  op->mutable_request()->mutable_key_value()->set_key(key.cdata(), key.size());
  op->mutable_request()->mutable_key_value()->set_type(REDIS_TYPE_TIMESERIES);
  op->mutable_request()->mutable_key_value()->add_subkey()->set_timestamp_subkey(timestamp);
  op->mutable_request()->mutable_key_value()->add_value(value.cdata(), value.size());
  return Status::OK();
}

Status ParseHMSet(YBRedisWriteOp *op, const RedisClientCommand& args) {
  DCHECK_EQ("hmset", to_lower_case(args[0]))
      << "Parsing hmset request where first arg is not hmset.";
  if (args.size() < 4 || args.size() % 2 == 1) {
    return STATUS_SUBSTITUTE(InvalidArgument,
        "wrong number of arguments for HMSET");
  }
  RETURN_NOT_OK(op->SetKey(args[1]));
  op->mutable_request()->set_allocated_set_request(new RedisSetRequestPB());
  op->mutable_request()->mutable_key_value()->set_type(REDIS_TYPE_HASH);
  op->mutable_request()->mutable_key_value()->set_key(args[1].cdata(), args[1].size());
  // We remove duplicates from the subkeys here.
  std::unordered_map<string, string> kv_map;
  for (int i = 2; i < args.size(); i += 2) {
    kv_map[string(args[i].cdata(), args[i].size())] =
        string(args[i + 1].cdata(), args[i + 1].size());
  }
  for (const auto& kv : kv_map) {
    op->mutable_request()->mutable_key_value()->add_subkey()->set_string_subkey(kv.first);
    op->mutable_request()->mutable_key_value()->add_value(kv.second);
  }
  return Status::OK();
}

template <class YBRedisOp>
Status ParseCollection(YBRedisOp *op,
                       const RedisClientCommand& args,
                       boost::optional<RedisDataType> type,
                       bool remove_duplicates = true) {
  const auto& key = args[1];
  RETURN_NOT_OK(op->SetKey(key));
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
      op->mutable_request()->mutable_key_value()->add_subkey()->set_string_subkey(val);
    }
  } else {
    for (size_t i = 2; i < args.size(); i++) {
      op->mutable_request()->mutable_key_value()->add_subkey()->set_string_subkey(args[i].cdata(),
                                                                                  args[i].size());
    }
  }
  return Status::OK();
}

Status ParseHDel(YBRedisWriteOp *op, const RedisClientCommand& args) {
  op->mutable_request()->set_allocated_del_request(new RedisDelRequestPB());
  return ParseCollection(op, args, REDIS_TYPE_HASH);
}

Status ParseSAdd(YBRedisWriteOp *op, const RedisClientCommand& args) {
  op->mutable_request()->set_allocated_add_request(new RedisAddRequestPB());
  return ParseCollection(op, args, REDIS_TYPE_SET);
}

Status ParseSRem(YBRedisWriteOp *op, const RedisClientCommand& args) {
  op->mutable_request()->set_allocated_del_request(new RedisDelRequestPB());
  return ParseCollection(op, args, REDIS_TYPE_SET);
}

Status ParseGetSet(YBRedisWriteOp *op, const RedisClientCommand& args) {
  const auto& key = args[1];
  const auto& value = args[2];
  RETURN_NOT_OK(op->SetKey(key));
  op->mutable_request()->set_allocated_getset_request(new RedisGetSetRequestPB());
  op->mutable_request()->mutable_key_value()->set_key(key.cdata(), key.size());
  op->mutable_request()->mutable_key_value()->add_value(value.cdata(), value.size());
  return Status::OK();
}

Status ParseAppend(YBRedisWriteOp* op, const RedisClientCommand& args) {
  const auto& key = args[1];
  const auto& value = args[2];
  RETURN_NOT_OK(op->SetKey(key));
  op->mutable_request()->set_allocated_append_request(new RedisAppendRequestPB());
  op->mutable_request()->mutable_key_value()->set_key(key.cdata(), key.size());
  op->mutable_request()->mutable_key_value()->add_value(value.cdata(), value.size());
  return Status::OK();
}

// Note: deleting only one key is supported using one command as of now.
Status ParseDel(YBRedisWriteOp* op, const RedisClientCommand& args) {
  const auto& key = args[1];
  RETURN_NOT_OK(op->SetKey(key));
  op->mutable_request()->set_allocated_del_request(new RedisDelRequestPB());
  op->mutable_request()->mutable_key_value()->set_key(key.cdata(), key.size());
  // We should be able to delete all types of top level keys
  op->mutable_request()->mutable_key_value()->set_type(REDIS_TYPE_NONE);
  return Status::OK();
}

Status ParseSetRange(YBRedisWriteOp* op, const RedisClientCommand& args) {
  const auto& key = args[1];
  const auto& value = args[3];
  RETURN_NOT_OK(op->SetKey(key));
  op->mutable_request()->set_allocated_set_range_request(new RedisSetRangeRequestPB());
  op->mutable_request()->mutable_key_value()->set_key(key.cdata(), key.size());
  op->mutable_request()->mutable_key_value()->add_value(value.cdata(), value.size());

  auto offset = ParseInt32(args[2], "offset");
  RETURN_NOT_OK(offset);
  op->mutable_request()->mutable_set_range_request()->set_offset(*offset);

  return Status::OK();
}

Status ParseIncr(YBRedisWriteOp* op, const RedisClientCommand& args) {
  const auto& key = args[1];
  RETURN_NOT_OK(op->SetKey(key));
  op->mutable_request()->set_allocated_incr_request(new RedisIncrRequestPB());
  op->mutable_request()->mutable_key_value()->set_key(key.cdata(), key.size());
  return Status::OK();
}

Status ParseGet(YBRedisReadOp* op, const RedisClientCommand& args) {
  op->mutable_request()->set_allocated_get_request(new RedisGetRequestPB());
  const auto& key = args[1];
  RETURN_NOT_OK(op->SetKey(key));
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
Status ParseHGetLikeCommands(YBRedisReadOp* op, const RedisClientCommand& args,
                             RedisGetRequestPB_GetRequestType request_type,
                             bool remove_duplicates = false) {
  op->mutable_request()->set_allocated_get_request(new RedisGetRequestPB());
  op->mutable_request()->mutable_get_request()->set_request_type(request_type);

  return ParseCollection(op, args, boost::none, remove_duplicates);
}

// TODO: Support MGET
Status ParseMGet(YBRedisReadOp* op, const RedisClientCommand& args) {
  return STATUS(InvalidCommand, "MGET command not yet supported");
}

Status ParseHGet(YBRedisReadOp* op, const RedisClientCommand& args) {
  return ParseHGetLikeCommands(op, args, RedisGetRequestPB_GetRequestType_HGET);
}

Status ParseTsGet(YBRedisReadOp* op, const RedisClientCommand& args) {
  op->mutable_request()->set_allocated_get_request(new RedisGetRequestPB());
  op->mutable_request()->mutable_get_request()->set_request_type(
      RedisGetRequestPB_GetRequestType_TSGET);

  const auto& key = args[1];
  RETURN_NOT_OK(op->SetKey(key));
  op->mutable_request()->mutable_key_value()->set_key(key.cdata(), key.size());
  int64_t timestamp;
  RETURN_NOT_OK(util::CheckedStoll(args[2], &timestamp));
  op->mutable_request()->mutable_key_value()->add_subkey()->set_timestamp_subkey(timestamp);

  return Status::OK();
}

Status ParseHStrLen(YBRedisReadOp* op, const RedisClientCommand& args) {
  return ParseHGetLikeCommands(op, args, RedisGetRequestPB_GetRequestType_HSTRLEN);
}

Status ParseHExists(YBRedisReadOp* op, const RedisClientCommand& args) {
  return ParseHGetLikeCommands(op, args, RedisGetRequestPB_GetRequestType_HEXISTS);
}

Status ParseHMGet(YBRedisReadOp* op, const RedisClientCommand& args) {
  return ParseHGetLikeCommands(op, args, RedisGetRequestPB_GetRequestType_HMGET);
}

Status ParseHGetAll(YBRedisReadOp* op, const RedisClientCommand& args) {
  return ParseHGetLikeCommands(op, args, RedisGetRequestPB_GetRequestType_HGETALL);
}

Status ParseHKeys(YBRedisReadOp* op, const RedisClientCommand& args) {
  return ParseHGetLikeCommands(op, args, RedisGetRequestPB_GetRequestType_HKEYS);
}

Status ParseHVals(YBRedisReadOp* op, const RedisClientCommand& args) {
  return ParseHGetLikeCommands(op, args, RedisGetRequestPB_GetRequestType_HVALS);
}

Status ParseHLen(YBRedisReadOp* op, const RedisClientCommand& args) {
  return ParseHGetLikeCommands(op, args, RedisGetRequestPB_GetRequestType_HLEN);
}

Status ParseSMembers(YBRedisReadOp* op, const RedisClientCommand& args) {
  return ParseHGetLikeCommands(op, args, RedisGetRequestPB_GetRequestType_SMEMBERS);
}

Status ParseSIsMember(YBRedisReadOp* op, const RedisClientCommand& args) {
  return ParseHGetLikeCommands(op, args, RedisGetRequestPB_GetRequestType_SISMEMBER);
}

Status ParseSCard(YBRedisReadOp* op, const RedisClientCommand& args) {
  return ParseHGetLikeCommands(op, args, RedisGetRequestPB_GetRequestType_SCARD);
}

Status ParseStrLen(YBRedisReadOp* op, const RedisClientCommand& args) {
  op->mutable_request()->set_allocated_strlen_request(new RedisStrLenRequestPB());
  const auto& key = args[1];
  RETURN_NOT_OK(op->SetKey(key));
  op->mutable_request()->mutable_key_value()->set_key(key.cdata(), key.size());
  return Status::OK();
}

// Note: Checking existence of only one key is supported as of now.
Status ParseExists(YBRedisReadOp* op, const RedisClientCommand& args) {
  op->mutable_request()->set_allocated_exists_request(new RedisExistsRequestPB());
  const auto& key = args[1];
  RETURN_NOT_OK(op->SetKey(key));
  op->mutable_request()->mutable_key_value()->set_key(key.cdata(), key.size());
  return Status::OK();
}

Status ParseGetRange(YBRedisReadOp* op, const RedisClientCommand& args) {
  op->mutable_request()->set_allocated_get_range_request(new RedisGetRangeRequestPB());
  const auto& key = args[1];
  RETURN_NOT_OK(op->SetKey(key));
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
Status RedisParser::NextCommand(const uint8_t** end_of_command) {
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

Status RedisParser::AdvanceToNextToken() {
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

Status RedisParser::Initial() {
  token_begin_ = pos_;
  state_ = *pos_ == '*' ? State::BULK_HEADER : State::SINGLE_LINE;
  return Status::OK();
}

Status RedisParser::SingleLine() {
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

Status RedisParser::BulkHeader() {
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

Status RedisParser::BulkArgumentSize() {
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

Status RedisParser::BulkArgumentBody() {
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

Status RedisParser::FindEndOfLine() {
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
Status RedisParser::ParseNumber(char prefix,
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
  auto number_begin = pointer_cast<const char*>(token_begin_ + 1);
  if (!isdigit(*number_begin)) { // disable skip of space characters by strtoll
    return STATUS_FORMAT(Corruption, "Number starts with invalid character: $0", *number_begin);
  }
  char* token_end;
  auto parsed_number = std::strtoll(number_begin, &token_end, 10);
  static_assert(sizeof(parsed_number) == sizeof(*out), "Expected size");
  const char* expected_stop = pointer_cast<const char*>(pos_ - kLineEndLength);
  if (token_end != expected_stop) {
    return STATUS_SUBSTITUTE(Corruption,
                             "$0 was failed to parse, extra data after number: $1",
                             name,
                             Slice(token_end, expected_stop).ToDebugString());
  }
  SCHECK_BOUNDS(parsed_number,
                min,
                max,
                Corruption,
                yb::Format("$0 out of expected range [$1, $2] : $3",
                           name, min, max, parsed_number));
  *out = static_cast<ptrdiff_t>(parsed_number);
  return Status::OK();
}

}  // namespace redisserver
}  // namespace yb
