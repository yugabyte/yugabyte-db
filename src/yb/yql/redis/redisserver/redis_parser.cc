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

#include "yb/yql/redis/redisserver/redis_parser.h"

#include <memory>
#include <string>

#include <boost/algorithm/string.hpp>

#include "yb/client/callbacks.h"
#include "yb/client/yb_op.h"

#include "yb/common/redis_protocol.pb.h"

#include "yb/gutil/casts.h"

#include "yb/util/split.h"
#include "yb/util/status.h"
#include "yb/util/status_format.h"
#include "yb/util/stol_utils.h"
#include "yb/util/string_case.h"

#include "yb/yql/redis/redisserver/redis_constants.h"

namespace yb {
namespace redisserver {

using yb::client::YBRedisWriteOp;
using yb::client::YBRedisReadOp;
using std::string;

namespace {

constexpr size_t kMaxNumberOfArgs = 1 << 20;
constexpr size_t kLineEndLength = 2;
constexpr size_t kMaxNumberLength = 25;
constexpr char kPositiveInfinity[] = "+inf";
constexpr char kNegativeInfinity[] = "-inf";

string to_lower_case(Slice slice) {
  return boost::to_lower_copy(slice.ToBuffer());
}

Status add_string_subkey(string subkey, RedisKeyValuePB* kv_pb) {
  kv_pb->add_subkey()->set_string_subkey(std::move(subkey));
  return Status::OK();
}

Status add_timestamp_subkey(const string &subkey, RedisKeyValuePB *kv_pb) {
  auto timestamp = CheckedStoll(subkey);
  RETURN_NOT_OK(timestamp);
  kv_pb->add_subkey()->set_timestamp_subkey(*timestamp);
  return Status::OK();
}

Status add_double_subkey(const string &subkey, RedisKeyValuePB *kv_pb) {
  auto double_key = CheckedStold(subkey);
  RETURN_NOT_OK(double_key);
  kv_pb->add_subkey()->set_double_subkey(*double_key);
  return Status::OK();
}

Result<int64_t> ParseInt64(const Slice& slice, const char* field) {
  auto result = CheckedStoll(slice);
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

Status ParseSet(YBRedisWriteOp *op, const RedisClientCommand& args) {
  if (args[1].empty()) {
    return STATUS_SUBSTITUTE(InvalidCommand,
        "A SET request must have a non empty key field");
  }
  op->mutable_request()->mutable_set_request(); // Allocates new RedisSetRequestPB().
  const auto& key = args[1];
  const auto& value = args[2];
  op->mutable_request()->mutable_key_value()->set_key(key.cdata(), key.size());
  op->mutable_request()->mutable_key_value()->add_value(value.cdata(), value.size());
  op->mutable_request()->mutable_key_value()->set_type(REDIS_TYPE_STRING);
  size_t idx = 3;
  while (idx < args.size()) {
    string upper_arg;
    if (args[idx].size() == 2) {
      ToUpperCase(args[idx].ToBuffer(), &upper_arg);
    }

    if (upper_arg == "EX" || upper_arg == "PX") {
      if (args.size() < idx + 2) {
        return STATUS_SUBSTITUTE(InvalidCommand,
            "Expected TTL field after the EX flag, no value found");
      }
      auto ttl_val = ParseInt64(args[idx + 1], "TTL");
      RETURN_NOT_OK(ttl_val);
      if (*ttl_val < kRedisMinTtlSetExSeconds || *ttl_val > kRedisMaxTtlSeconds) {
        return STATUS_FORMAT(InvalidCommand,
            "TTL field $0 is not within valid bounds", args[idx + 1]);
      }
      const int64_t milliseconds_per_unit =
          upper_arg == "EX" ? MonoTime::kMillisecondsPerSecond : 1;
      op->mutable_request()->mutable_set_request()->set_ttl(*ttl_val * milliseconds_per_unit);
      idx += 2;
    } else if (upper_arg == "XX") {
      op->mutable_request()->mutable_set_request()->set_mode(REDIS_WRITEMODE_UPDATE);
      idx += 1;
    } else if (upper_arg == "NX") {
      op->mutable_request()->mutable_set_request()->set_mode(REDIS_WRITEMODE_INSERT);
      idx += 1;
    } else {
      return STATUS_FORMAT(InvalidCommand,
          "Unidentified argument $0 found while parsing set command", args[idx]);
    }
  }
  return Status::OK();
}

Status ParseSetNX(YBRedisWriteOp *op, const RedisClientCommand& args) {
  if (args[1].empty()) {
    return STATUS_SUBSTITUTE(InvalidCommand,
        "A SETNX request must have a non empty key field");
  }

  const auto& key = args[1];
  const auto& value = args[2];

  auto key_value = op->mutable_request()->mutable_key_value();
  key_value->set_key(key.cdata(), key.size());
  key_value->add_value(value.cdata(), value.size());
  key_value->set_type(REDIS_TYPE_STRING);

  auto set_request = op->mutable_request()->mutable_set_request();
  set_request->set_mode(REDIS_WRITEMODE_INSERT);
  // SETNX returns 1 / 0 (instead of OK / (nil) in 'SET k v NX' command).
  set_request->set_expect_ok_response(false);
  return Status::OK();
}

// TODO: support MSET
Status ParseMSet(YBRedisWriteOp *op, const RedisClientCommand& args) {
  if (args.size() < 3 || args.size() % 2 == 0) {
    return STATUS_SUBSTITUTE(InvalidCommand,
        "An MSET request must have at least 3, odd number of arguments, found $0", args.size());
  }
  return STATUS(InvalidCommand, "MSET command not yet supported");
}

Status ParseHSet(YBRedisWriteOp *op, const RedisClientCommand& args) {
  const auto& key = args[1];
  const auto& subkey = args[2];
  const auto& value = args[3];

  op->mutable_request()->mutable_set_request(); // Allocates new RedisSetRequestPB().
  op->mutable_request()->mutable_key_value()->set_key(key.cdata(), key.size());
  op->mutable_request()->mutable_key_value()->set_type(REDIS_TYPE_HASH);
  op->mutable_request()->mutable_key_value()->add_subkey()->set_string_subkey(subkey.cdata(),
                                                                              subkey.size());
  op->mutable_request()->mutable_key_value()->add_value(value.cdata(), value.size());
  return Status::OK();
}


Status ParseHIncrBy(YBRedisWriteOp *op, const RedisClientCommand& args) {
  const auto& key = args[1];
  const auto& subkey = args[2];
  const auto& incr_by = ParseInt64(args[3], "INCR_BY");
  RETURN_NOT_OK(incr_by);
  op->mutable_request()->mutable_incr_request()->set_increment_int(*incr_by);
  op->mutable_request()->mutable_key_value()->set_key(key.cdata(), key.size());
  op->mutable_request()->mutable_key_value()->set_type(REDIS_TYPE_HASH);
  op->mutable_request()->mutable_key_value()->add_subkey()->set_string_subkey(subkey.cdata(),
                                                                              subkey.size());
  return Status::OK();
}

Status ParseZAddOptions(
    SortedSetOptionsPB *options, const RedisClientCommand& args, size_t *idx) {
  // While we keep seeing flags, set the appropriate field in options and increment idx. When
  // we finally stop seeing flags, the idx will be set to that token for later parsing.
  // Note that we can see duplicate flags, and it should have the same behavior as seeing the
  // flag once.
  while (*idx < args.size()) {
    if (boost::iequals(args[*idx].ToBuffer(), kCH)) {
      options->set_ch(true);
    } else if (boost::iequals(args[*idx].ToBuffer(), kINCR)) {
      options->set_incr(true);
    } else if (boost::iequals(args[*idx].ToBuffer(), kNX)) {
      if (options->update_options() == SortedSetOptionsPB_UpdateOptions_XX) {
        return STATUS_SUBSTITUTE(InvalidArgument,
                                 "XX and NX options at the same time are not compatible");
      }
      options->set_update_options(SortedSetOptionsPB_UpdateOptions_NX);
    } else if (boost::iequals(args[*idx].ToBuffer(), kXX)) {
      if (options->update_options() == SortedSetOptionsPB_UpdateOptions_NX) {
        return STATUS_SUBSTITUTE(InvalidArgument,
                                 "XX and NX options at the same time are not compatible");
      }
      options->set_update_options(SortedSetOptionsPB_UpdateOptions_XX);
    } else {
      // We have encountered a non-option token, return.
      return Status::OK();
    }
    *idx = *idx + 1;
  }
  return Status::OK();
}

template <typename AddSubKey>
Status ParseHMSetLikeCommands(YBRedisWriteOp *op, const RedisClientCommand& args,
                              const RedisDataType& type,
                              AddSubKey add_sub_key) {
  if (args.size() < 4 || (args.size() % 2 == 1 && type == REDIS_TYPE_HASH)) {
    return STATUS_SUBSTITUTE(InvalidArgument,
                             "wrong number of arguments: $0 for command: $1", args.size(),
                             string(args[0].cdata(), args[0].size()));
  }

  op->mutable_request()->mutable_set_request(); // Allocates new RedisSetRequestPB().
  op->mutable_request()->mutable_key_value()->set_type(type);
  op->mutable_request()->mutable_key_value()->set_key(args[1].cdata(), args[1].size());

  if (type == REDIS_TYPE_HASH) {
    op->mutable_request()->mutable_set_request()->set_expect_ok_response(true);
  }

  size_t start_idx = 2;
  if (type == REDIS_TYPE_SORTEDSET) {
    RETURN_NOT_OK(ParseZAddOptions(
        op->mutable_request()->mutable_set_request()->mutable_sorted_set_options(),
        args, &start_idx));

    // If the INCR flag is set, can only have one [score member] pair.
    if (op->request().set_request().sorted_set_options().incr() && (args.size() - start_idx) != 2) {
      return STATUS_SUBSTITUTE(InvalidArgument,
                               "wrong number of tokens after INCR flag specified: Need 2 but found "
                                   "$0 for command: $1", args.size() - start_idx,
                               string(args[0].cdata(), args[0].size()));
    }
  }

  // Need [score member] to come in pairs.
  if ((args.size() - start_idx) % 2 == 1 || args.size() - start_idx == 0) {
    return STATUS_SUBSTITUTE(InvalidArgument,
                             "Expect even and non-zero number of arguments "
                             "for command: $0, found $1",
                             string(args[0].cdata(), args[0].size()), args.size() - start_idx);
  }

  std::unordered_map<string, string> kv_map;
  for (size_t i = start_idx; i < args.size(); i += 2) {
    // EXPIRE_AT/EXPIRE_IN only supported for redis timeseries currently.
    if (type == REDIS_TYPE_TIMESERIES) {
      string upper_arg;
      ToUpperCase(args[i].ToBuffer(), &upper_arg);
      if (upper_arg == kExpireAt || upper_arg == kExpireIn) {
        if (i + 2 != args.size()) {
          return STATUS_SUBSTITUTE(InvalidCommand, "$0 should be at the end of the command",
                                   string(args[i].cdata(), args[i].size()));
        }
        auto temp = CheckedStoll(args[i + 1]);
        RETURN_NOT_OK(temp);
        int64_t ttl = 0;
        if (upper_arg == kExpireIn) {
          ttl = *temp;
          if (ttl > kRedisMaxTtlSeconds || ttl < kRedisMinTtlSetExSeconds) {
            return STATUS_SUBSTITUTE(InvalidCommand, "TTL: $0 needs be in the range [$1, $2]", ttl,
                                     kRedisMinTtlSetExSeconds, kRedisMaxTtlSeconds);
          }
        } else {
          auto current_time = GetCurrentTimeMicros() / MonoTime::kMicrosecondsPerSecond;
          ttl = *temp - current_time;
          if (ttl > kRedisMaxTtlSeconds || ttl < kRedisMinTtlSetExSeconds) {
            return STATUS_SUBSTITUTE(InvalidCommand, "EXPIRE_AT: $0 needs be in the range [$1, $2]",
                                     *temp,
                                     kRedisMinTtlSetExSeconds + current_time,
                                     kRedisMaxTtlSeconds + current_time);
          }
        }

        // Need to pass ttl in milliseconds, user supplied values are in seconds.
        op->mutable_request()->mutable_set_request()->set_ttl(
            ttl * MonoTime::kMillisecondsPerSecond);
      } else {
        kv_map[args[i].ToBuffer()] = args[i + 1].ToBuffer();
      }
    } else if (type == REDIS_TYPE_SORTEDSET) {
      // For sorted sets, we store the mapping from values to scores, since values are distinct
      // but scores aren't.
      kv_map[args[i + 1].ToBuffer()] = args[i].ToBuffer();
    } else {
      kv_map[args[i].ToBuffer()] = args[i + 1].ToBuffer();
    }
  }

  for (const auto& kv : kv_map) {
    auto req_kv = op->mutable_request()->mutable_key_value();
    if (type == REDIS_TYPE_SORTEDSET) {
      // Since the mapping is values to scores, need to reverse when creating the request.
      RETURN_NOT_OK(add_sub_key(kv.second, req_kv));
      req_kv->add_value(kv.first);
    } else {
      RETURN_NOT_OK(add_sub_key(kv.first, req_kv));
      req_kv->add_value(kv.second);
    }
  }
  return Status::OK();
}

Status ParseHMSet(YBRedisWriteOp *op, const RedisClientCommand& args) {
  DCHECK_EQ("hmset", to_lower_case(args[0]))
      << "Parsing hmset request where first arg is not hmset.";
  return ParseHMSetLikeCommands(op, args, REDIS_TYPE_HASH, add_string_subkey);
}

Status ParseTsAdd(YBRedisWriteOp *op, const RedisClientCommand& args) {
  DCHECK_EQ("tsadd", to_lower_case(args[0]))
    << "Parsing hmset request where first arg is not hmset.";
  return ParseHMSetLikeCommands(op, args, REDIS_TYPE_TIMESERIES,
                                add_timestamp_subkey);
}

Status ParseZAdd(YBRedisWriteOp *op, const RedisClientCommand& args) {
  DCHECK_EQ("zadd", to_lower_case(args[0]))
    << "Parsing zadd request where first arg is not zadd.";
  return ParseHMSetLikeCommands(op, args, REDIS_TYPE_SORTEDSET, add_double_subkey);
}

Status ParsePush(YBRedisWriteOp *op, const RedisClientCommand& args, RedisSide side) {
  op->mutable_request()->mutable_push_request()->set_side(side);
  op->mutable_request()->mutable_key_value()->set_key(args[1].cdata(), args[1].size());
  op->mutable_request()->mutable_key_value()->set_type(REDIS_TYPE_LIST);

  auto mutable_key = op->mutable_request()->mutable_key_value();
  for (size_t i = 2; i < args.size(); ++i) {
    mutable_key->add_value(args[i].cdata(), args[i].size());
  }
  return Status::OK();
}

Status ParseLPush(YBRedisWriteOp *op, const RedisClientCommand& args) {
  return ParsePush(op, args, REDIS_SIDE_LEFT);
}

Status ParseRPush(YBRedisWriteOp *op, const RedisClientCommand& args) {
  return ParsePush(op, args, REDIS_SIDE_RIGHT);
}

template <typename YBRedisOp, typename AddSubKey>
Status ParseCollection(YBRedisOp *op,
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
    op->mutable_request()->mutable_key_value()->mutable_subkey()->Reserve(
        narrow_cast<int>(subkey_set.size()));
    for (const auto &val : subkey_set) {
      RETURN_NOT_OK(add_sub_key(val, op->mutable_request()->mutable_key_value()));
    }
  } else {
    op->mutable_request()->mutable_key_value()->mutable_subkey()->Reserve(
        narrow_cast<int>(args.size() - 2));
    for (size_t i = 2; i < args.size(); i++) {
      RETURN_NOT_OK(add_sub_key(string(args[i].cdata(), args[i].size()),
                                op->mutable_request()->mutable_key_value()));
    }
  }
  return Status::OK();
}

Status ParseHDel(YBRedisWriteOp *op, const RedisClientCommand& args) {
  op->mutable_request()->mutable_del_request(); // Allocates new RedisDelRequestPB().
  return ParseCollection(op, args, REDIS_TYPE_HASH, add_string_subkey);
}

Status ParseTsRem(YBRedisWriteOp *op, const RedisClientCommand& args) {
  op->mutable_request()->mutable_del_request(); // Allocates new RedisDelRequestPB().
  return ParseCollection(op, args, REDIS_TYPE_TIMESERIES, add_timestamp_subkey);
}

Status ParseZRem(YBRedisWriteOp *op, const RedisClientCommand& args) {
  op->mutable_request()->mutable_del_request(); // Allocates new RedisDelRequestPB().
  return ParseCollection(op, args, REDIS_TYPE_SORTEDSET, add_string_subkey);
}

Status ParseSAdd(YBRedisWriteOp *op, const RedisClientCommand& args) {
  op->mutable_request()->mutable_add_request(); // Allocates new RedisAddRequestPB().
  return ParseCollection(op, args, REDIS_TYPE_SET, add_string_subkey);
}

Status ParseSRem(YBRedisWriteOp *op, const RedisClientCommand& args) {
  op->mutable_request()->mutable_del_request(); // Allocates new RedisDelRequestPB().
  return ParseCollection(op, args, REDIS_TYPE_SET, add_string_subkey);
}

Status ParsePop(YBRedisWriteOp *op, const RedisClientCommand& args, RedisSide side) {
  op->mutable_request()->mutable_pop_request()->set_side(side);
  op->mutable_request()->mutable_key_value()->set_key(args[1].cdata(), args[1].size());
  op->mutable_request()->mutable_key_value()->set_type(REDIS_TYPE_LIST);
  return Status::OK();
}

Status ParseLPop(YBRedisWriteOp *op, const RedisClientCommand& args) {
  return ParsePop(op, args, REDIS_SIDE_LEFT);
}

Status ParseRPop(YBRedisWriteOp *op, const RedisClientCommand& args) {
  return ParsePop(op, args, REDIS_SIDE_RIGHT);
}

Status ParseGetSet(YBRedisWriteOp *op, const RedisClientCommand& args) {
  const auto& key = args[1];
  const auto& value = args[2];
  op->mutable_request()->mutable_getset_request(); // Allocates new RedisGetSetRequestPB().
  op->mutable_request()->mutable_key_value()->set_key(key.cdata(), key.size());
  op->mutable_request()->mutable_key_value()->add_value(value.cdata(), value.size());
  return Status::OK();
}

Status ParseAppend(YBRedisWriteOp* op, const RedisClientCommand& args) {
  const auto& key = args[1];
  const auto& value = args[2];
  op->mutable_request()->mutable_append_request(); // Allocates new RedisAppendRequestPB().
  op->mutable_request()->mutable_key_value()->set_key(key.cdata(), key.size());
  op->mutable_request()->mutable_key_value()->add_value(value.cdata(), value.size());
  return Status::OK();
}

// Note: deleting only one key is supported using one command as of now.
Status ParseDel(YBRedisWriteOp* op, const RedisClientCommand& args) {
  const auto& key = args[1];
  op->mutable_request()->mutable_del_request(); // Allocates new RedisDelRequestPB().
  op->mutable_request()->mutable_key_value()->set_key(key.cdata(), key.size());
  // We should be able to delete all types of top level keys
  op->mutable_request()->mutable_key_value()->set_type(REDIS_TYPE_NONE);
  return Status::OK();
}

Status ParseSetRange(YBRedisWriteOp* op, const RedisClientCommand& args) {
  const auto& key = args[1];
  const auto& value = args[3];
  op->mutable_request()->mutable_key_value()->set_key(key.cdata(), key.size());
  op->mutable_request()->mutable_key_value()->add_value(value.cdata(), value.size());

  auto offset = ParseInt32(args[2], "offset");
  RETURN_NOT_OK(offset);
  // TODO: Should we have an upper bound?
  // A very large offset would allocate a lot of memory and maybe crash
  if (*offset < 0) {
    return STATUS_SUBSTITUTE(InvalidArgument,
        "offset field of SETRANGE must be non-negative, found: $0", *offset);
  }
  op->mutable_request()->mutable_set_range_request()->set_offset(*offset);

  return Status::OK();
}

Status ParseIncr(YBRedisWriteOp* op, const RedisClientCommand& args) {
  const auto& key = args[1];
  op->mutable_request()->mutable_incr_request()->set_increment_int(1);
  op->mutable_request()->mutable_key_value()->set_key(key.cdata(), key.size());
  op->mutable_request()->mutable_key_value()->set_type(REDIS_TYPE_STRING);
  return Status::OK();
}

Status ParseIncrBy(YBRedisWriteOp* op, const RedisClientCommand& args) {
  const auto& key = args[1];
  const auto& incr_by = ParseInt64(args[2], "INCR_BY");
  RETURN_NOT_OK(incr_by);
  op->mutable_request()->mutable_incr_request()->set_increment_int(*incr_by);
  op->mutable_request()->mutable_key_value()->set_key(key.cdata(), key.size());
  op->mutable_request()->mutable_key_value()->set_type(REDIS_TYPE_STRING);
  return Status::OK();
}

Status ParseGet(YBRedisReadOp* op, const RedisClientCommand& args) {
  const auto& key = args[1];
  if (key.empty()) {
    return STATUS_SUBSTITUTE(InvalidCommand,
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
  op->mutable_request()->mutable_get_request()->set_request_type(request_type);
  return ParseCollection(op, args, boost::none, add_string_subkey, remove_duplicates);
}

// TODO: Support MGET
Status ParseMGet(YBRedisReadOp* op, const RedisClientCommand& args) {
  return STATUS(InvalidCommand, "MGET command not yet supported");
}

Status ParseHGet(YBRedisReadOp* op, const RedisClientCommand& args) {
  return ParseHGetLikeCommands(op, args, RedisGetRequestPB_GetRequestType_HGET);
}

Status ParseTsBoundArg(const Slice& slice, RedisSubKeyBoundPB* bound_pb,
                       RedisCollectionGetRangeRequestPB::GetRangeRequestType request_type,
                       bool exclusive) {
  string bound(slice.cdata(), slice.size());
  if (bound == kPositiveInfinity) {
    bound_pb->set_infinity_type(RedisSubKeyBoundPB_InfinityType_POSITIVE);
  } else if (bound == kNegativeInfinity) {
    bound_pb->set_infinity_type(RedisSubKeyBoundPB_InfinityType_NEGATIVE);
  } else {
    bound_pb->set_is_exclusive(exclusive);
    switch (request_type) {
      case RedisCollectionGetRangeRequestPB_GetRangeRequestType_TSREVRANGEBYTIME:
        FALLTHROUGH_INTENDED;
      case RedisCollectionGetRangeRequestPB_GetRangeRequestType_TSRANGEBYTIME: {
        auto ts_bound = CheckedStoll(slice);
        RETURN_NOT_OK(ts_bound);
        bound_pb->mutable_subkey_bound()->set_timestamp_subkey(*ts_bound);
        break;
      }
      case RedisCollectionGetRangeRequestPB_GetRangeRequestType_ZRANGEBYSCORE: {
        auto double_bound = CheckedStold(slice);
        RETURN_NOT_OK(double_bound);
        bound_pb->mutable_subkey_bound()->set_double_subkey(*double_bound);
        break;
      }
      default:
        return STATUS_SUBSTITUTE(InvalidArgument, "Invalid request type: $0", request_type);
    }

  }
  return Status::OK();
}

Status
ParseIndexBoundArg(const Slice& slice, RedisIndexBoundPB* bound_pb) {
  auto index_bound = CheckedStoll(slice);
  RETURN_NOT_OK(index_bound);
  bound_pb->set_index(*index_bound);
  return Status::OK();
}

Status
ParseTsSubKeyBound(const Slice& slice, RedisSubKeyBoundPB* bound_pb,
                   RedisCollectionGetRangeRequestPB::GetRangeRequestType request_type) {
  if (slice.empty()) {
    return STATUS(InvalidCommand, "range bound key cannot be empty");
  }

  if (slice[0] == '(' && slice.size() > 1) {
    auto slice_copy = slice;
    slice_copy.remove_prefix(1);
    RETURN_NOT_OK(ParseTsBoundArg(slice_copy, bound_pb, request_type, /* exclusive */ true));
  } else {
    RETURN_NOT_OK(ParseTsBoundArg(slice, bound_pb, request_type, /* exclusive */ false));
  }
  return Status::OK();
}

Status ParseIndexBound(const Slice& slice, RedisIndexBoundPB* bound_pb) {
  if (slice.empty()) {
    return STATUS(InvalidArgument, "range bound index cannot be empty");
  }
  RETURN_NOT_OK(ParseIndexBoundArg(slice, bound_pb));
  return Status::OK();
}

Status ParseTsCard(YBRedisReadOp* op, const RedisClientCommand& args) {
  return ParseHGetLikeCommands(op, args, RedisGetRequestPB_GetRequestType_TSCARD);
}

Status ParseTsLastN(YBRedisReadOp* op, const RedisClientCommand& args) {
  // TSLastN is basically TSRangeByTime -INF, INF with a limit on number of entries. Note that
  // there is a subtle difference here since TSRangeByTime iterates on entries from highest to
  // lowest and hence we end up returning the highest N entries. This operation is more like
  // TSRevRangeByTime -INF, INF with a limit (Note that TSRevRangeByTime is not implemented).
  op->mutable_request()->mutable_get_collection_range_request()->set_request_type(
      RedisCollectionGetRangeRequestPB_GetRangeRequestType_TSRANGEBYTIME);
  const auto& key = args[1];
  auto limit = ParseInt32(args[2], "limit");
  RETURN_NOT_OK(limit);
  if ((*limit) <= 0) {
    return STATUS_SUBSTITUTE(InvalidArgument,
        "$0 field $1 is not within valid bounds", "limit", args[2].ToDebugString());
  }
  op->mutable_request()->mutable_key_value()->set_key(key.ToBuffer());
  op->mutable_request()->set_range_request_limit(*limit);
  op->mutable_request()->mutable_subkey_range()->mutable_lower_bound()->set_infinity_type
      (RedisSubKeyBoundPB_InfinityType_NEGATIVE);
  op->mutable_request()->mutable_subkey_range()->mutable_upper_bound()->set_infinity_type
      (RedisSubKeyBoundPB_InfinityType_POSITIVE);
  return Status::OK();
}

Status ParseTsRangeByTime(YBRedisReadOp* op, const RedisClientCommand& args) {
  op->mutable_request()->mutable_get_collection_range_request()->set_request_type(
      RedisCollectionGetRangeRequestPB_GetRangeRequestType_TSRANGEBYTIME);

  const auto& key = args[1];
  RETURN_NOT_OK(ParseTsSubKeyBound(
      args[2],
      op->mutable_request()->mutable_subkey_range()->mutable_lower_bound(),
      RedisCollectionGetRangeRequestPB_GetRangeRequestType_TSRANGEBYTIME));
  RETURN_NOT_OK(ParseTsSubKeyBound(
      args[3],
      op->mutable_request()->mutable_subkey_range()->mutable_upper_bound(),
      RedisCollectionGetRangeRequestPB_GetRangeRequestType_TSRANGEBYTIME));

  op->mutable_request()->mutable_key_value()->set_key(key.ToBuffer());
  return Status::OK();
}

Status ParseTsRevRangeByTime(YBRedisReadOp* op, const RedisClientCommand& args) {
  op->mutable_request()->mutable_get_collection_range_request()->set_request_type(
      RedisCollectionGetRangeRequestPB_GetRangeRequestType_TSREVRANGEBYTIME);

  const auto& key = args[1];
      RETURN_NOT_OK(ParseTsSubKeyBound(
      args[2],
      op->mutable_request()->mutable_subkey_range()->mutable_lower_bound(),
      RedisCollectionGetRangeRequestPB_GetRangeRequestType_TSREVRANGEBYTIME));
      RETURN_NOT_OK(ParseTsSubKeyBound(
      args[3],
      op->mutable_request()->mutable_subkey_range()->mutable_upper_bound(),
      RedisCollectionGetRangeRequestPB_GetRangeRequestType_TSREVRANGEBYTIME));

  op->mutable_request()->mutable_key_value()->set_key(key.ToBuffer());

  if (args.size() > 4) {
    if (args.size() != 6) {
      return STATUS_SUBSTITUTE(InvalidCommand,
                               "Invalid number of arguments. Command should have 4 or 6 arguments");
    }
    string upper_arg;
    ToUpperCase(args[4].ToBuffer(), &upper_arg);
    if (upper_arg != "LIMIT") {
      return STATUS_SUBSTITUTE(InvalidArgument,
                               "Invalid argument $0. Expecting $1", args[4].ToBuffer(), "limit");
    }
    auto limit = ParseInt32(args[5], "limit");
    RETURN_NOT_OK(limit);
    if ((*limit) <= 0) {
      return STATUS_SUBSTITUTE(InvalidArgument,
                               "$0 field $1 is not within valid bounds", "limit",
                               args[5].ToDebugString());
    }
    op->mutable_request()->set_range_request_limit(*limit);
  }

  return Status::OK();
}

Status ParseWithScores(const Slice& slice, RedisCollectionGetRangeRequestPB* request) {
  if(!boost::iequals(slice.ToBuffer(), kWithScores)) {
    return STATUS_SUBSTITUTE(InvalidArgument, "unexpected argument $0", slice.ToBuffer());
  }
  request->set_with_scores(true);
  return Status::OK();
}

Status ParseRangeByScoreOptions(YBRedisReadOp* op, const RedisClientCommand& args) {
  auto args_size = args.size();
  for (size_t i = 4; i < args_size; ++i) {
    string upper_arg;
    ToUpperCase(args[i].ToBuffer(), &upper_arg);
    if (upper_arg == "LIMIT") {
      if (i >= args_size - 2) {
        return STATUS_SUBSTITUTE(InvalidArgument, "Not enough args passed into LIMIT clause, "
            "expected 2 but found $0", args_size - (i + 1));
      }
      auto offset = VERIFY_RESULT(ParseInt64(args[++i], "offset"));
      auto limit = VERIFY_RESULT(ParseInt32(args[++i], "count"));
      op->mutable_request()->mutable_index_range()->mutable_lower_bound()->set_index(offset);
      op->mutable_request()->set_range_request_limit(limit);
    } else if (upper_arg == "WITHSCORES") {
      op->mutable_request()->mutable_get_collection_range_request()->set_with_scores(true);
    } else {
      return STATUS_SUBSTITUTE(InvalidArgument, "Invalid argument $0", args[i].ToBuffer());
    }
  }
  return Status::OK();
}

Status ParseZRangeByScore(YBRedisReadOp* op, const RedisClientCommand& args) {
  op->mutable_request()->mutable_get_collection_range_request()->set_request_type(
      RedisCollectionGetRangeRequestPB_GetRangeRequestType_ZRANGEBYSCORE);

  const auto& key = args[1];
  RETURN_NOT_OK(ParseTsSubKeyBound(
  args[2],
  op->mutable_request()->mutable_subkey_range()->mutable_lower_bound(),
  RedisCollectionGetRangeRequestPB_GetRangeRequestType_ZRANGEBYSCORE));
  RETURN_NOT_OK(ParseTsSubKeyBound(
  args[3],
  op->mutable_request()->mutable_subkey_range()->mutable_upper_bound(),
  RedisCollectionGetRangeRequestPB_GetRangeRequestType_ZRANGEBYSCORE));
  op->mutable_request()->mutable_key_value()->set_key(key.ToBuffer());

  return ParseRangeByScoreOptions(op, args);
}

Status ParseIndexBasedQuery(
    YBRedisReadOp* op,
    const RedisClientCommand& args,
    RedisCollectionGetRangeRequestPB::GetRangeRequestType request_type) {
  if (args.size() <= 5) {
    op->mutable_request()->mutable_get_collection_range_request()->set_request_type(request_type);

    const auto& key = args[1];
    RETURN_NOT_OK(ParseIndexBound(
    args[2],
    op->mutable_request()->mutable_index_range()->mutable_lower_bound()));
    RETURN_NOT_OK(ParseIndexBound(
    args[3],
    op->mutable_request()->mutable_index_range()->mutable_upper_bound()));
    op->mutable_request()->mutable_key_value()->set_key(key.ToBuffer());
    if (args.size() == 5) {
      RETURN_NOT_OK(ParseWithScores(
      args[4],
      op->mutable_request()->mutable_get_collection_range_request()));
    }
    return Status::OK();
  }
  return STATUS(InvalidArgument, "Expected at most 5 arguments, found $0",
                  std::to_string(args.size()));
}

Status ParseZRange(YBRedisReadOp* op, const RedisClientCommand& args) {
  return ParseIndexBasedQuery(
      op, args, RedisCollectionGetRangeRequestPB_GetRangeRequestType_ZRANGE);
}

Status ParseZRevRange(YBRedisReadOp* op, const RedisClientCommand& args) {
  return ParseIndexBasedQuery(
      op, args, RedisCollectionGetRangeRequestPB_GetRangeRequestType_ZREVRANGE);
}

Status ParseTsGet(YBRedisReadOp* op, const RedisClientCommand& args) {
  op->mutable_request()->mutable_get_request()->set_request_type(
      RedisGetRequestPB_GetRequestType_TSGET);

  const auto& key = args[1];
  op->mutable_request()->mutable_key_value()->set_key(key.cdata(), key.size());
  auto timestamp = CheckedStoll(args[2]);
  RETURN_NOT_OK(timestamp);
  op->mutable_request()->mutable_key_value()->add_subkey()->set_timestamp_subkey(*timestamp);

  return Status::OK();
}

Status ParseZScore(YBRedisReadOp* op, const RedisClientCommand& args) {
  op->mutable_request()->mutable_get_request()->set_request_type(
      RedisGetRequestPB_GetRequestType_ZSCORE);

  const auto& key = args[1];
  op->mutable_request()->mutable_key_value()->set_key(key.cdata(), key.size());
  auto member = args[2].ToBuffer();
  op->mutable_request()->mutable_key_value()->add_subkey()->set_string_subkey(member);

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

Status ParseLLen(YBRedisReadOp* op, const RedisClientCommand& args) {
  return ParseHGetLikeCommands(op, args, RedisGetRequestPB_GetRequestType_LLEN);
}

Status ParseZCard(YBRedisReadOp* op, const RedisClientCommand& args) {
  return ParseHGetLikeCommands(op, args, RedisGetRequestPB_GetRequestType_ZCARD);
}

Status ParseStrLen(YBRedisReadOp* op, const RedisClientCommand& args) {
  op->mutable_request()->mutable_strlen_request(); // Allocates new RedisStrLenRequestPB().
  const auto& key = args[1];
  op->mutable_request()->mutable_key_value()->set_key(key.cdata(), key.size());
  return Status::OK();
}

// Note: Checking existence of only one key is supported as of now.
Status ParseExists(YBRedisReadOp* op, const RedisClientCommand& args) {
  op->mutable_request()->mutable_exists_request(); // Allocates new RedisExistsRequestPB().
  const auto& key = args[1];
  op->mutable_request()->mutable_key_value()->set_key(key.cdata(), key.size());
  return Status::OK();
}

Status ParseGetRange(YBRedisReadOp* op, const RedisClientCommand& args) {
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

Status ParseExpire(YBRedisWriteOp* op,
                   const RedisClientCommand& args,
                   const bool using_millis) {
  const auto& key = args[1];
  auto ttl = ParseInt64(args[2], "TTL");
  RETURN_NOT_OK(ttl);
  // If the TTL is not positive, we immediately delete.
  if (*ttl <= 0) {
      op->mutable_request()->mutable_del_request(); // Allocates new RedisDelRequestPB().
      op->mutable_request()->mutable_key_value()->set_key(key.cdata(), key.size());
      op->mutable_request()->mutable_key_value()->set_type(REDIS_TYPE_NONE);
      return Status::OK();
  }
  *ttl *= using_millis ? 1 : MonoTime::kMillisecondsPerSecond;
  if (*ttl < kRedisMinTtlMillis || *ttl > kRedisMaxTtlMillis) {
    return STATUS_FORMAT(InvalidCommand,
        "TTL field $0 is not within valid bounds", args[2]);
  }
  op->mutable_request()->mutable_key_value()->set_key(key.cdata(), key.size());
  op->mutable_request()->mutable_set_ttl_request()->set_ttl(*ttl);
  return Status::OK();
}

Status ParseExpire(YBRedisWriteOp* op, const RedisClientCommand& args) {
  return ParseExpire(op, args, false);
}

Status ParsePExpire(YBRedisWriteOp* op, const RedisClientCommand& args) {
  return ParseExpire(op, args, true);
}

Status ParsePersist(YBRedisWriteOp* op, const RedisClientCommand& args) {
  const auto& key = args[1];
  op->mutable_request()->mutable_key_value()->set_key(key.cdata(), key.size());
  op->mutable_request()->mutable_set_ttl_request()->set_ttl(-1);
  return Status::OK();
}

Status ParseExpireAt(YBRedisWriteOp* op,
                           const RedisClientCommand& args,
                           const bool using_millis) {
  const auto& key = args[1];
  auto expiration = VERIFY_RESULT(ParseInt64(args[2], "expiration"));
  // If the TTL is not positive, we immediately delete.
  if (expiration <= 0) {
      op->mutable_request()->mutable_del_request(); // Allocates new RedisDelRequestPB().
      op->mutable_request()->mutable_key_value()->set_key(key.cdata(), key.size());
      op->mutable_request()->mutable_key_value()->set_type(REDIS_TYPE_NONE);
      return Status::OK();
  }
  expiration *= using_millis ? 1 : MonoTime::kMillisecondsPerSecond;
  op->mutable_request()->mutable_key_value()->set_key(key.cdata(), key.size());
  op->mutable_request()->mutable_set_ttl_request()->set_absolute_time(expiration);
  return Status::OK();
}

Status ParseExpireAt(YBRedisWriteOp* op, const RedisClientCommand& args) {
  return ParseExpireAt(op, args, /* using_millis */ false);
}

Status ParsePExpireAt(YBRedisWriteOp* op, const RedisClientCommand& args) {
  return ParseExpireAt(op, args, /* using_millis */ true);
}

Status ParseSetEx(YBRedisWriteOp* op,
                  const RedisClientCommand& args,
                  const bool using_millis) {
  const auto& key = args[1];
  const auto value = args[3];
  auto ttl = VERIFY_RESULT(ParseInt64(args[2], "TTL"));
  if (ttl <= 0) {
    op->mutable_request()->mutable_no_op_request(); // Allocates new RedisNoOpRequestPB().
    return Status::OK();
  }
  ttl *= using_millis ? 1 : MonoTime::kMillisecondsPerSecond;
  if (ttl < kRedisMinTtlMillis || ttl > kRedisMaxTtlMillis) {
    return STATUS_FORMAT(InvalidCommand,
        "TTL field $0 is not within valid bounds", args[3]);
  }
  op->mutable_request()->mutable_key_value()->set_key(key.cdata(), key.size());
  op->mutable_request()->mutable_key_value()->add_value(value.cdata(), value.size());
  op->mutable_request()->mutable_key_value()->set_type(REDIS_TYPE_STRING);
  op->mutable_request()->mutable_set_request()->set_ttl(ttl);
  return Status::OK();
}

Status ParseSetEx(YBRedisWriteOp* op, const RedisClientCommand& args) {
  return ParseSetEx(op, args, false);
}

Status ParsePSetEx(YBRedisWriteOp* op, const RedisClientCommand& args) {
  return ParseSetEx(op, args, true);
}

Status ParseTtl(YBRedisReadOp* op,
                const RedisClientCommand& args,
                const bool return_seconds) {
  const auto& key = args[1];
  op->mutable_request()->mutable_key_value()->set_key(key.cdata(), key.size());
  op->mutable_request()->mutable_get_ttl_request()->set_return_seconds(return_seconds);
  return Status::OK();
}

Status ParseTtl(YBRedisReadOp* op, const RedisClientCommand& args) {
  return ParseTtl(op, args, true);
}

Status ParsePTtl(YBRedisReadOp* op, const RedisClientCommand& args) {
  return ParseTtl(op, args, false);
}

// Begin of input is going to be consumed, so we should adjust our pointers.
// Since the beginning of input is being consumed by shifting the remaining bytes to the
// beginning of the buffer.
void RedisParser::Consume(size_t count) {
  pos_ -= count;

  if (token_begin_ != kNoToken) {
    token_begin_ -= count;
  }
}

// New data arrived, so update the end of available bytes.
void RedisParser::Update(const IoVecs& data) {
  source_ = data;
  full_size_ = IoVecsFullSize(data);
  DCHECK_LE(pos_, full_size_);
}

// Parse next command.
Result<size_t> RedisParser::NextCommand() {
  while (pos_ != full_size_) {
    incomplete_ = false;
    Status status = AdvanceToNextToken();
    if (!status.ok()) {
      return status;
    }
    if (incomplete_) {
      pos_ = full_size_;
      return 0;
    }
    if (state_ == State::FINISHED) {
      state_ = State::INITIAL;
      return pos_;
    }
  }
  return 0;
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
  LOG(FATAL) << "Unexpected parser state: " << to_underlying(state_);
}

Status RedisParser::Initial() {
  token_begin_ = pos_;
  state_ = char_at_offset(pos_) == '*' ? State::BULK_HEADER : State::SINGLE_LINE;
  return Status::OK();
}

Status RedisParser::SingleLine() {
  auto status = FindEndOfLine();
  if (!status.ok() || incomplete_) {
    return status;
  }
  auto start = token_begin_;
  auto finish = pos_ - 2;
  while (start < finish && isspace(char_at_offset(start))) {
    ++start;
  }
  if (start >= finish) {
    return STATUS(InvalidArgument, "Empty line");
  }
  if (args_) {
    // Args is supported only when parsing from single block of data.
    // Because we parse prepared call data in this case, that is contained in a single buffer.
    DCHECK_EQ(source_.size(), 1);
    RETURN_NOT_OK(util::SplitArgs(Slice(offset_to_pointer(start), finish - start), args_));
  }
  state_ = State::FINISHED;
  return Status::OK();
}

Status RedisParser::BulkHeader() {
  auto status = FindEndOfLine();
  if (!status.ok() || incomplete_) {
    return status;
  }
  auto num_args = VERIFY_RESULT(ParseNumber(
      '*', 1, kMaxNumberOfArgs, "Number of lines in multiline"));
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
  auto current_size = VERIFY_RESULT(ParseNumber('$', 0, kMaxRedisValueSize, "Argument size"));
  state_ = State::BULK_ARGUMENT_BODY;
  token_begin_ = pos_;
  current_argument_size_ = current_size;
  return Status::OK();
}

Status RedisParser::BulkArgumentBody() {
  auto desired_position = token_begin_ + current_argument_size_ + kLineEndLength;
  if (desired_position > full_size_) {
    incomplete_ = true;
    pos_ = full_size_;
    return Status::OK();
  }
  if (char_at_offset(desired_position - 1) != '\n' ||
      char_at_offset(desired_position - 2) != '\r') {
    return STATUS(NetworkError, "No \\r\\n after bulk");
  }
  if (args_) {
    // Args is supported only when parsing from single block of data.
    // Because we parse prepared call data in this case, that is contained in a single buffer.
    DCHECK_EQ(source_.size(), 1);
    args_->emplace_back(offset_to_pointer(token_begin_), current_argument_size_);
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
  auto p = offset_to_idx_and_local_offset(pos_);

  size_t new_line_offset = pos_;
  while (p.first != source_.size()) {
    auto begin = IoVecBegin(source_[p.first]) + p.second;
    auto new_line = static_cast<const char*>(memchr(
        begin, '\n', IoVecEnd(source_[p.first]) - begin));
    if (new_line) {
      new_line_offset += new_line - begin;
      break;
    }
    new_line_offset += source_[p.first].iov_len - p.second;
    ++p.first;
    p.second = 0;
  }

  incomplete_ = p.first == source_.size();
  if (!incomplete_) {
    if (new_line_offset == token_begin_) {
      return STATUS(NetworkError, "End of line at the beginning of a Redis command");
    }
    if (char_at_offset(new_line_offset - 1) != '\r') {
      return STATUS(NetworkError, "\\n is not prefixed with \\r");
    }
    pos_ = ++new_line_offset;
  }
  return Status::OK();
}

std::pair<size_t, size_t> RedisParser::offset_to_idx_and_local_offset(size_t offset) const {
  // We assume that there are at most 2 blocks of data.
  if (offset < source_[0].iov_len) {
    return std::pair<size_t, size_t>(0, offset);
  }

  offset -= source_[0].iov_len;
  size_t idx = offset / source_[1].iov_len;
  offset -= idx * source_[1].iov_len;

  return std::pair<size_t, size_t>(idx + 1, offset);
}

const char* RedisParser::offset_to_pointer(size_t offset) const {
  auto p = offset_to_idx_and_local_offset(offset);
  return IoVecBegin(source_[p.first]) + p.second;
}

// Parses number with specified bounds.
// Number is located in separate line, and contain prefix before actual number.
// Line starts at token_begin_ and pos_ is a start of next line.
Result<ptrdiff_t> RedisParser::ParseNumber(char prefix,
                                           ptrdiff_t min,
                                           ptrdiff_t max,
                                           const char* name) {
  if (char_at_offset(token_begin_) != prefix) {
    return STATUS_FORMAT(Corruption,
                         "Invalid character before number, expected: $0, but found: $1",
                         prefix, char_at_offset(token_begin_));
  }
  auto number_begin = token_begin_ + 1;
  auto expected_stop = pos_ - kLineEndLength;
  if (expected_stop - number_begin > kMaxNumberLength) {
    return STATUS_FORMAT(
        Corruption, "Too long $0 of length $1", name, expected_stop - number_begin);
  }
  number_buffer_.reserve(kMaxNumberLength);
  IoVecsToBuffer(source_, number_begin, expected_stop, &number_buffer_);
  number_buffer_.push_back(0);
  auto parsed_number = VERIFY_RESULT(CheckedStoll(
      Slice(number_buffer_.data(), number_buffer_.size() - 1)));
  static_assert(sizeof(parsed_number) == sizeof(ptrdiff_t), "Expected size");
  SCHECK_BOUNDS(parsed_number,
                min,
                max,
                Corruption,
                yb::Format("$0 out of expected range [$1, $2] : $3",
                           name, min, max, parsed_number));
  return static_cast<ptrdiff_t>(parsed_number);
}

void RedisParser::SetArgs(boost::container::small_vector_base<Slice>* args) {
  DCHECK_EQ(source_.size(), 1);
  args_ = args;
}

}  // namespace redisserver
}  // namespace yb
