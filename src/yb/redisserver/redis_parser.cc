// Copyright (c) YugaByte, Inc.

#include <memory>
#include <string>

#include "yb/client/client.h"
#include "yb/client/yb_op.h"
#include "yb/common/redis_protocol.pb.h"
#include "yb/gutil/strings/substitute.h"
#include "yb/redisserver/redis_constants.h"
#include "yb/redisserver/redis_parser.h"
#include "yb/util/status.h"

namespace yb {
namespace redisserver {

using yb::client::YBTable;
using yb::client::YBRedisWriteOp;
using yb::client::YBRedisReadOp;
using std::vector;
using std::shared_ptr;
using std::string;

constexpr int64_t kMaxTTLSec = std::numeric_limits<int64_t>::max() / 1000000;

string to_lower_case(Slice slice) {
  string lower_string = slice.ToString();
  std::transform(lower_string.begin(), lower_string.end(), lower_string.begin(), ::tolower);
  return lower_string;
}

Status ParseSet(YBRedisWriteOp *op, const std::vector<Slice> &args) {
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

Status ParseHSet(YBRedisWriteOp *op, const std::vector<Slice> &args) {
  DCHECK_EQ("hset", to_lower_case(args[0]))
      << "Parsing hset request where first arg is not hset.";
  if (args.size() != 4) {
    return STATUS_SUBSTITUTE(InvalidArgument,
        "An HSet request must have exactly 4 arguments, found $0", args.size());
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

Status ParseGetSet(YBRedisWriteOp *op, const std::vector<Slice> &args) {
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

Status ParseAppend(YBRedisWriteOp* op, const std::vector<Slice>& args) {
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
Status ParseDel(YBRedisWriteOp* op, const std::vector<Slice>& args) {
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
  return Status::OK();
}

Status ParseSetRange(YBRedisWriteOp* op, const std::vector<Slice>& args) {
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

Status ParseIncr(YBRedisWriteOp* op, const std::vector<Slice>& args) {
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

Status ParseGet(YBRedisReadOp* op, const std::vector<Slice>& args) {
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
  return Status::OK();
}

Status ParseHGet(YBRedisReadOp* op, const std::vector<Slice>& args) {
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
  return Status::OK();
}

Status ParseStrLen(YBRedisReadOp* op, const std::vector<Slice>& args) {
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
Status ParseExists(YBRedisReadOp* op, const std::vector<Slice>& args) {
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

Status ParseGetRange(YBRedisReadOp* op, const std::vector<Slice>& args) {
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

}  // namespace redisserver
}  // namespace yb
