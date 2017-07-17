// Copyright (c) YugaByte, Inc.

#include <memory>
#include <string>

#include "yb/client/client.h"
#include "yb/client/yb_op.h"

#include "yb/common/redis_protocol.pb.h"

#include "yb/gutil/strings/substitute.h"

#include "yb/redisserver/redis_constants.h"
#include "yb/redisserver/redis_parser.h"

#include "yb/util/split.h"
#include "yb/util/status.h"

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

} // namespace

string to_lower_case(Slice slice) {
  string lower_string = slice.ToString();
  std::transform(lower_string.begin(), lower_string.end(), lower_string.begin(), ::tolower);
  return lower_string;
}

Status ParseSet(YBRedisWriteOp *op, const RedisClientCommand& args) {
  DCHECK_EQ("set", to_lower_case(args[0]))
      << "Parsing set request where first arg is not set.";
  if (args.size() < 3) {
    return STATUS_SUBSTITUTE(InvalidArgument,
        "A SET request must have at least 3 arguments, found $0", args.size());
  }
  if (args[1].empty()) {
    return STATUS_SUBSTITUTE(InvalidArgument,
        "A SET request must have a non empty key field");
  }
  op->mutable_request()->set_allocated_set_request(new RedisSetRequestPB());
  const string string_key = args[1].ToString();
  const string string_value = args[2].ToString();
  op->mutable_request()->mutable_key_value()->set_key(string_key);
  RETURN_NOT_OK(op->SetKey(string_key));
  if (string_value.empty()) {
      return STATUS_SUBSTITUTE(InvalidArgument,
        "A SET request must have non empty value field");
  }
  op->mutable_request()->mutable_key_value()->add_value(string_value);
  int idx = 3;
  while (idx < args.size()) {
    if (args[idx] == "EX" || args[idx] == "PX") {
      if (args.size() < idx + 2) {
        return STATUS_SUBSTITUTE(InvalidArgument,
            "Expected TTL field after the EX flag, no vale found");
      }
      string ttl_string = args[idx + 1].ToString();
      int64_t ttl_val;
      try {
        ttl_val = std::stoll(ttl_string);
        if (ttl_val <= 0 || ttl_val > kMaxTTLSec) {
          return STATUS_SUBSTITUTE(InvalidArgument,
              "TTL field $0 is not within valid bounds", ttl_string);
        }
      } catch (std::invalid_argument e) {
        return STATUS_SUBSTITUTE(InvalidArgument,
            "TTL field $0 is not parsable as a valid number", ttl_string);
      }
      const int64_t milliseconds_per_unit = args[idx] == "EX" ? 1000 : 1;
      op->mutable_request()->mutable_set_request()->set_ttl(ttl_val * milliseconds_per_unit);
      idx += 2;
    } else if (args[idx] == "XX") {
      op->mutable_request()->mutable_set_request()->set_mode(REDIS_WRITEMODE_UPDATE);
      idx += 1;
    } else if (args[idx] == "NX") {
      op->mutable_request()->mutable_set_request()->set_mode(REDIS_WRITEMODE_INSERT);
      idx += 1;
    } else {
      return STATUS_SUBSTITUTE(InvalidArgument,
          "Unidentified argument $0 found while parsing set command", args[idx].ToString());
    }
  }
  return Status::OK();
}

// TODO: support MSET
Status ParseMSet(YBRedisWriteOp *op, const RedisClientCommand& args) {
  DCHECK_EQ("mset", to_lower_case(args[0]))
      << "Parsing mset request where first arg is not mset.";
  if (args.size() < 3 || args.size() % 2 == 0) {
    return STATUS_SUBSTITUTE(InvalidArgument,
        "An MSET request must have at least 3, odd number of arguments, found $0", args.size());
  }
  return STATUS(InvalidCommand, "MSET command not yet supported");
}

Status ParseHSet(YBRedisWriteOp *op, const RedisClientCommand& args) {
  DCHECK_EQ("hset", to_lower_case(args[0]))
      << "Parsing hset request where first arg is not hset.";
  if (args.size() != 4) {
    return STATUS_SUBSTITUTE(InvalidArgument,
        "An HSET request must have exactly 4 arguments, found $0", args.size());
  }
  const string string_key = args[1].ToString();
  const string string_subkey = args[2].ToString();
  const string string_value = args[3].ToString();
  RETURN_NOT_OK(op->SetKey(string_key));
  op->mutable_request()->set_allocated_set_request(new RedisSetRequestPB());
  op->mutable_request()->mutable_key_value()->set_key(string_key);
  op->mutable_request()->mutable_key_value()->set_type(REDIS_TYPE_HASH);
  op->mutable_request()->mutable_key_value()->add_subkey(string_subkey);
  op->mutable_request()->mutable_key_value()->add_value(string_value);
  return Status::OK();
}

Status ParseHMSet(YBRedisWriteOp *op, const RedisClientCommand& args) {
  DCHECK_EQ("hmset", to_lower_case(args[0]))
      << "Parsing hmset request where first arg is not hmset.";
  if (args.size() < 4 || args.size() % 2 == 1) {
    return STATUS_SUBSTITUTE(InvalidArgument,
        "An HMSET request must have at least 4, even number of arguments, found $0", args.size());
  }
  RETURN_NOT_OK(op->SetKey(args[1].ToString()));
  op->mutable_request()->set_allocated_set_request(new RedisSetRequestPB());
  op->mutable_request()->mutable_key_value()->set_type(REDIS_TYPE_HASH);
  op->mutable_request()->mutable_key_value()->set_key(args[1].ToString());
  for (int i = 2; i < args.size(); i += 2) {
    op->mutable_request()->mutable_key_value()->add_subkey(args[i].ToString());
    op->mutable_request()->mutable_key_value()->add_value(args[i+1].ToString());
  }
  return Status::OK();
}

Status ParseHDel(YBRedisWriteOp *op, const RedisClientCommand& args) {
  const string string_key = args[1].ToString();
  RETURN_NOT_OK(op->SetKey(string_key));
  op->mutable_request()->set_allocated_del_request(new RedisDelRequestPB());
  op->mutable_request()->mutable_key_value()->set_key(string_key);
  op->mutable_request()->mutable_key_value()->set_type(REDIS_TYPE_HASH);
  for (size_t i = 2; i < args.size(); i++) {
    op->mutable_request()->mutable_key_value()->add_subkey(args[i].ToString());
  }
  return Status::OK();
}

Status ParseSAdd(YBRedisWriteOp *op, const RedisClientCommand& args) {
  DCHECK_EQ("sadd", to_lower_case(args[0]))
      << "Parsing sadd request where first arg is not sadd.";
  if (args.size() < 3) {
    return STATUS_SUBSTITUTE(InvalidArgument,
        "An SADD request must have at least 3 arguments, found $0", args.size());
  }
  const string string_key = args[1].ToString();
  RETURN_NOT_OK(op->SetKey(string_key));
  op->mutable_request()->set_allocated_add_request(new RedisAddRequestPB());
  op->mutable_request()->mutable_key_value()->set_key(string_key);
  op->mutable_request()->mutable_key_value()->set_type(REDIS_TYPE_SET);
  for (size_t i = 2; i < args.size(); i++) {
    op->mutable_request()->mutable_key_value()->add_subkey(args[i].ToString());
  }
  return Status::OK();
}

Status ParseSRem(YBRedisWriteOp *op, const RedisClientCommand& args) {
  const string string_key = args[1].ToString();
  RETURN_NOT_OK(op->SetKey(string_key));
  op->mutable_request()->set_allocated_del_request(new RedisDelRequestPB());
  op->mutable_request()->mutable_key_value()->set_key(string_key);
  op->mutable_request()->mutable_key_value()->set_type(REDIS_TYPE_SET);
  for (size_t i = 2; i < args.size(); i++) {
    op->mutable_request()->mutable_key_value()->add_subkey(args[i].ToString());
  }
  return Status::OK();
}

Status ParseGetSet(YBRedisWriteOp *op, const RedisClientCommand& args) {
  DCHECK_EQ("getset", to_lower_case(args[0]))
      << "Parsing getset request where first arg is not getset.";
  if (args.size() != 3) {
    return STATUS_SUBSTITUTE(InvalidArgument,
        "A GETSET request must have exactly 3 arguments, found $0", args.size());
  }
  const string string_key = args[1].ToString();
  const string string_value = args[2].ToString();
  RETURN_NOT_OK(op->SetKey(string_key));
  op->mutable_request()->set_allocated_getset_request(new RedisGetSetRequestPB());
  op->mutable_request()->mutable_key_value()->set_key(string_key);
  op->mutable_request()->mutable_key_value()->add_value(string_value);
  return Status::OK();
}

Status ParseAppend(YBRedisWriteOp* op, const RedisClientCommand& args) {
  DCHECK_EQ("append", to_lower_case(args[0]))
      << "Parsing append request where first arg is not append.";
  if (args.size() != 3) {
    return STATUS_SUBSTITUTE(InvalidArgument,
        "An APPEND request must have exactly 3 arguments, found $0", args.size());
  }
  const string string_key = args[1].ToString();
  const string string_value = args[2].ToString();
  RETURN_NOT_OK(op->SetKey(string_key));
  op->mutable_request()->set_allocated_append_request(new RedisAppendRequestPB());
  op->mutable_request()->mutable_key_value()->set_key(string_key);
  op->mutable_request()->mutable_key_value()->add_value(string_value);
  return Status::OK();
}

// Note: deleting only one key is supported using one command as of now.
Status ParseDel(YBRedisWriteOp* op, const RedisClientCommand& args) {
  DCHECK_EQ("del", to_lower_case(args[0]))
      << "Parsing del request where first arg is not del.";
  if (args.size() != 2) {
    return STATUS_SUBSTITUTE(InvalidArgument,
        "A DEL request must have exactly 2 arguments, found $0", args.size());
  }
  const string string_key = args[1].ToString();
  RETURN_NOT_OK(op->SetKey(string_key));
  op->mutable_request()->set_allocated_del_request(new RedisDelRequestPB());
  op->mutable_request()->mutable_key_value()->set_key(string_key);
  op->mutable_request()->mutable_key_value()->set_type(REDIS_TYPE_STRING);
  return Status::OK();
}

Status ParseSetRange(YBRedisWriteOp* op, const RedisClientCommand& args) {
  DCHECK_EQ("setrange", to_lower_case(args[0]))
      << "Parsing setrange request where first arg is not setrange.";
  if (args.size() != 4) {
    return STATUS_SUBSTITUTE(InvalidArgument,
        "A GETSET request must have exactly 4 arguments, found $0", args.size());
  }
  const string string_key = args[1].ToString();
  const string string_value = args[3].ToString();
  RETURN_NOT_OK(op->SetKey(string_key));
  op->mutable_request()->set_allocated_set_range_request(new RedisSetRangeRequestPB());
  op->mutable_request()->mutable_key_value()->set_key(string_key);
  op->mutable_request()->mutable_key_value()->add_value(string_value);

  const string offset_string = args[2].ToString();
  int32_t offset_val;
  try {
    offset_val = std::stoi(offset_string);
  } catch (std::invalid_argument e) {
    return STATUS_SUBSTITUTE(InvalidArgument,
        "Offset field $0 is not parsable as a valid number", offset_string);
  } catch (std::out_of_range e) {
    return STATUS_SUBSTITUTE(InvalidArgument,
        "Offset field $0 is not within valid bounds", offset_string);
  }
  op->mutable_request()->mutable_set_range_request()->set_offset(offset_val);

  return Status::OK();
}

Status ParseIncr(YBRedisWriteOp* op, const RedisClientCommand& args) {
  DCHECK_EQ("incr", to_lower_case(args[0]))
      << "Parsing incr request where first arg is not incr.";
  if (args.size() != 2) {
    return STATUS_SUBSTITUTE(InvalidArgument,
        "An INCR request must have exactly 2 arguments, found $0", args.size());
  }
  const string string_key = args[1].ToString();
  RETURN_NOT_OK(op->SetKey(string_key));
  op->mutable_request()->set_allocated_incr_request(new RedisIncrRequestPB());
  op->mutable_request()->mutable_key_value()->set_key(string_key);
  return Status::OK();
}

Status ParseGet(YBRedisReadOp* op, const RedisClientCommand& args) {
  DCHECK_EQ("get", to_lower_case(args[0]))
      << "Parsing get request where first arg is not get.";
  // TODO: Can remove some checks, eg. arg[0] == "GET" and key non-empty
  if (args.size() != 2) {
    return STATUS_SUBSTITUTE(InvalidArgument,
        "A GET request must have exactly two arguments, found $0", args.size());
  }
  op->mutable_request()->set_allocated_get_request(new RedisGetRequestPB());
  const string string_key = args[1].ToString();
  RETURN_NOT_OK(op->SetKey(string_key));
  if (string_key.empty()) {
    return STATUS_SUBSTITUTE(InvalidArgument,
        "A SET request must have non empty value field");
  }
  op->mutable_request()->mutable_key_value()->set_key(string_key);
  op->mutable_request()->mutable_get_request()->set_request_type(
      RedisGetRequestPB_GetRequestType_GET);
  return Status::OK();
}

// TODO: Support MGET
Status ParseMGet(YBRedisReadOp* op, const RedisClientCommand& args) {
  DCHECK_EQ("mget", to_lower_case(args[0]))
      << "Parsing mget request where first arg is not mget.";
  if (args.size() < 2) {
    return STATUS_SUBSTITUTE(InvalidArgument,
        "A MGET request must have at least two arguments, found $0", args.size());
  }
  return STATUS(InvalidCommand, "MGET command not yet supported");;
}

Status ParseHGet(YBRedisReadOp* op, const RedisClientCommand& args) {
  DCHECK_EQ("hget", to_lower_case(args[0]))
      << "Parsing hget request where first arg is not hget.";
  if (args.size() != 3) {
    return STATUS_SUBSTITUTE(InvalidArgument,
        "A HGET request must have exactly 3 arguments, found $0", args.size());
  }
  op->mutable_request()->set_allocated_get_request(new RedisGetRequestPB());
  const string string_key = args[1].ToString();
  const string string_subkey = args[2].ToString();
  RETURN_NOT_OK(op->SetKey(string_key));
  op->mutable_request()->mutable_key_value()->set_key(string_key);
  op->mutable_request()->mutable_key_value()->set_type(REDIS_TYPE_HASH);
  op->mutable_request()->mutable_key_value()->add_subkey(string_subkey);
  op->mutable_request()->mutable_get_request()->set_request_type(
      RedisGetRequestPB_GetRequestType_HGET);
  return Status::OK();
}

Status ParseHMGet(YBRedisReadOp* op, const RedisClientCommand& args) {
  DCHECK_EQ("hmget", to_lower_case(args[0]))
      << "Parsing hmget request where first arg is not hmget.";
  if (args.size() < 3) {
    return STATUS_SUBSTITUTE(InvalidArgument,
        "A HMGET request must have at least three arguments, found $0", args.size());
  }
  op->mutable_request()->set_allocated_get_request(new RedisGetRequestPB());
  RETURN_NOT_OK(op->SetKey(args[1].ToString()));
  op->mutable_request()->mutable_key_value()->set_key(args[1].ToString());
  op->mutable_request()->mutable_key_value()->set_type(REDIS_TYPE_HASH);
  op->mutable_request()->mutable_get_request()->set_request_type(
      RedisGetRequestPB_GetRequestType_HMGET);
  for (int i = 2; i < args.size(); i++) {
    op->mutable_request()->mutable_key_value()->add_subkey(args[i].ToString());
  }
  return Status::OK();
}

Status ParseHGetAll(YBRedisReadOp* op, const RedisClientCommand& args) {
  DCHECK_EQ("hgetall", to_lower_case(args[0]))
      << "Parsing hgetall request where first arg is not hgetall.";
  if (args.size() != 2) {
    return STATUS_SUBSTITUTE(InvalidArgument,
        "A HGETALL request must have exactly two arguments, found $0", args.size());
  }
  op->mutable_request()->set_allocated_get_request(new RedisGetRequestPB());
  RETURN_NOT_OK(op->SetKey(args[1].ToString()));
  op->mutable_request()->mutable_key_value()->set_key(args[1].ToString());
  op->mutable_request()->mutable_key_value()->set_type(REDIS_TYPE_HASH);
  op->mutable_request()->mutable_get_request()->set_request_type(
      RedisGetRequestPB_GetRequestType_HGETALL);
  return Status::OK();
}

Status ParseSMembers(YBRedisReadOp* op, const RedisClientCommand& args) {
  DCHECK_EQ("smembers", to_lower_case(args[0]))
      << "Parsing smembers request where first arg is not smembers.";
  if (args.size() != 2) {
    return STATUS_SUBSTITUTE(InvalidArgument,
        "A SMEMBERS request must have exactly two arguments, found $0", args.size());
  }
  op->mutable_request()->set_allocated_get_request(new RedisGetRequestPB());
  RETURN_NOT_OK(op->SetKey(args[1].ToString()));
  op->mutable_request()->mutable_key_value()->set_key(args[1].ToString());
  op->mutable_request()->mutable_key_value()->set_type(REDIS_TYPE_SET);
  op->mutable_request()->mutable_get_request()->set_request_type(
      RedisGetRequestPB_GetRequestType_SMEMBERS);
  return Status::OK();
}

Status ParseStrLen(YBRedisReadOp* op, const RedisClientCommand& args) {
  DCHECK_EQ("strlen", to_lower_case(args[0]))
      << "Parsing strlen request where first arg is not strlen.";
  if (args.size() != 2) {
    return STATUS_SUBSTITUTE(InvalidArgument,
        "A STRLEN request must have exactly 2 arguments, found $0", args.size());
  }
  op->mutable_request()->set_allocated_strlen_request(new RedisStrLenRequestPB());
  const string string_key = args[1].ToString();
  RETURN_NOT_OK(op->SetKey(string_key));
  op->mutable_request()->mutable_key_value()->set_key(string_key);
  return Status::OK();
}

// Note: Checking existence of only one key is supported as of now.
Status ParseExists(YBRedisReadOp* op, const RedisClientCommand& args) {
  DCHECK_EQ("exists", to_lower_case(args[0]))
      << "Parsing exists request where first arg is not exists.";
  if (args.size() != 2) {
    return STATUS_SUBSTITUTE(InvalidArgument,
        "An EXISTS request must have exactly 2 arguments, found $0", args.size());
  }
  op->mutable_request()->set_allocated_exists_request(new RedisExistsRequestPB());
  const string string_key = args[1].ToString();
  RETURN_NOT_OK(op->SetKey(string_key));
  op->mutable_request()->mutable_key_value()->set_key(string_key);
  return Status::OK();
}

Status ParseGetRange(YBRedisReadOp* op, const RedisClientCommand& args) {
  DCHECK_EQ("getrange", to_lower_case(args[0]))
      << "Parsing getrange request where first arg is not getrange.";
  if (args.size() != 4) {
    return STATUS_SUBSTITUTE(InvalidArgument,
        "A GETRANGE request must have exactly 4 arguments, found $0", args.size());
  }
  op->mutable_request()->set_allocated_get_range_request(new RedisGetRangeRequestPB());
  const string string_key = args[1].ToString();
  RETURN_NOT_OK(op->SetKey(string_key));
  op->mutable_request()->mutable_key_value()->set_key(string_key);

  const string start_string = args[2].ToString();
  int32_t start_val;
  try {
    start_val = std::stoi(start_string);
  } catch (std::invalid_argument e) {
    return STATUS_SUBSTITUTE(InvalidArgument,
        "Start field $0 is not parsable as a valid number", start_string);
  } catch (std::out_of_range e) {
    return STATUS_SUBSTITUTE(InvalidArgument,
        "Start field $0 is not within valid bounds", start_string);
  }
  op->mutable_request()->mutable_get_range_request()->set_start(start_val);

  const string end_string = args[3].ToString();
  int32_t end_val;
  try {
    end_val = std::stoi(end_string);
  } catch (std::invalid_argument e) {
    return STATUS_SUBSTITUTE(InvalidArgument,
        "End field $0 is not parsable as a valid number", end_string);
  } catch (std::out_of_range e) {
    return STATUS_SUBSTITUTE(InvalidArgument,
        "End field $0 is not within valid bounds", end_string);
  }
  op->mutable_request()->mutable_get_range_request()->set_end(end_val);

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
  if (args_) {
    RETURN_NOT_OK(util::SplitArgs(Slice(token_begin_, pos_), args_));
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
  char* token_end;
  auto parsed_number =
      std::strtoll(pointer_cast<const char*>(token_begin_ + 1), &token_end, 10);
  static_assert(sizeof(parsed_number) == sizeof(*out), "Expected size");
  const char* expected_stop = pointer_cast<const char*>(pos_ - kLineEndLength);
  if (token_end != expected_stop) {
    return STATUS_SUBSTITUTE(NetworkError,
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
