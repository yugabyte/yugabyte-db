//
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
//
#include "yb/yql/redis/redisserver/redis_encoding.h"

#include "yb/util/logging.h"
#include <google/protobuf/repeated_field.h>

#include "yb/gutil/strings/numbers.h"
#include "yb/util/ref_cnt_buffer.h"

using namespace std::literals; // NOLINT

namespace yb {
namespace redisserver {

const std::string kNilResponse = "$-1\r\n";
const std::string kOkResponse = "+OK\r\n";
const std::string kRedisVersionInfo = "999.999.999";
const std::string kInfoResponse =
    "# Server\r\n"
    "redis_version:" + kRedisVersionInfo + "\r\n"
    "# Replication\r\n"
    "role:master\r\n"
    "connected_slaves:0\r\n"
    "master_replid:0000000000000000000000000000000000000000\r\n"
    "master_replid2:0000000000000000000000000000000000000000\r\n"
    "master_repl_offset:0\r\n"
    "second_repl_offset:-1\r\n"
    "repl_backlog_active:0\r\n"
    "repl_backlog_size:0\r\n"
    "repl_backlog_first_byte_offset:0\r\n"
    "repl_backlog_histlen:0\r\n";

namespace {

size_t NumberLength(size_t number) {
  if (number >= 10000000000000000000ULL) {
    return 20;
  }
  size_t result = 0;
  size_t temp = 1;
  while (temp <= number) {
    temp *= 10;
    ++result;
  }
  return std::max<size_t>(result, 1);
}

template <class Ch>
typename std::enable_if<std::is_same<Ch, char>::value, size_t>::type
CatOne(Ch ch, size_t len) {
  return len + 1;
}

template <class Ch>
typename std::enable_if<std::is_same<Ch, char>::value, uint8_t*>::type
CatOne(Ch ch, uint8_t* pos) {
  *pos = ch;
  return pos + 1;
}

size_t CatOne(size_t num, size_t len) {
  return len + NumberLength(num);
}

// Important notice: FastUInt64ToBufferLeft adds 0 to end of buffer, it is ok with redis,
// because we always add \r\n, but it could be issue in general case.
uint8_t* CatOne(size_t num, uint8_t* pos) {
  return pointer_cast<uint8_t*>(FastUInt64ToBufferLeft(num, pointer_cast<char*>(pos)));
}

size_t CatOne(int64_t num, size_t len) {
  size_t unum;
  if (num < 0) {
    ++len;
    unum = -num;
  } else {
    unum = num;
  }
  return len + NumberLength(unum);
}

// Important notice: FastInt64ToBufferLeft adds 0 to end of buffer, it is ok with redis,
// because we always add \r\n, but it could be issue in general case.
uint8_t* CatOne(int64_t num, uint8_t* pos) {
  return pointer_cast<uint8_t*>(FastInt64ToBufferLeft(num, pointer_cast<char*>(pos)));
}

template <size_t N>
size_t CatOne(const char str[N], size_t len) {
  return len + N - 1;
}

template <size_t N>
uint8_t* CatOne(const char str[N], uint8_t* pos) {
  memcpy(pos, str, N - 1);
  return pos + N - 1;
}

size_t CatOne(const std::string& str, size_t len) {
  return len + str.size();
}

uint8_t* CatOne(const std::string& str, uint8_t* pos) {
  memcpy(pos, str.data(), str.size());
  return pos + str.size();
}

template <class Out, class A1>
Out DoCat(Out out, A1&& a1) {
  return CatOne(a1, out);
}

template <class Out, class A1, class... Args>
Out DoCat(Out out, A1&& a1, Args&&... args) {
  return DoCat(CatOne(a1, out), std::forward<Args>(args)...);
}

const std::string kNewLine = "\r\n"s;

template <class Collection, class Out>
Out ProcessArray(const Collection& collection, Out out) {
  out = CatOne('*', out);
  out = CatOne(static_cast<size_t>(collection.size()), out);
  out = CatOne(kNewLine, out);
  for (const auto& element : collection) {
    if (element.empty()) {
      out = CatOne(kNilResponse, out);
    } else {
      out = CatOne('$', out);
      out = CatOne(element.size(), out);
      out = CatOne(kNewLine, out);
      out = CatOne(element, out);
      out = CatOne(kNewLine, out);
    }
  }
  return out;
}

template <class Collection, class Out>
Out ProcessEncodedArray(const Collection& collection, Out out) {
  out = CatOne('*', out);
  out = CatOne(static_cast<size_t>(collection.size()), out);
  out = CatOne(kNewLine, out);
  for (const auto& element : collection) {
    out = CatOne(element, out);
  }
  return out;
}

template <class Out>
Out ProcessInteger(const std::string& input, Out out) {
  return DoCat(out, ':', input, kNewLine);
}

template <class Out>
Out ProcessInteger(int64_t input, Out out) {
  return DoCat(out, ':', input, kNewLine);
}

template <class Out>
Out ProcessSimpleString(const std::string& input, Out out) {
  return DoCat(out, '+', input, kNewLine);
}

template <class Out>
Out ProcessError(const std::string& message, Out out) {
  return DoCat(out, '-', message, kNewLine);
}

template <class Out>
Out ProcessBulkString(const std::string& input, Out out) {
  size_t len = input.length();
  return DoCat(out, '$', len, kNewLine, input, kNewLine);
}

template <class Out>
Out ProcessEncoded(const std::string& input, Out out) {
  return DoCat(out, input);
}

} // namespace

#define DO_REDIS_PRIMITIVES_DEFINE(name, type) \
  RefCntBuffer BOOST_PP_CAT(EncodeAs, name)(type input) { \
    static constexpr size_t kZero = 0; \
    size_t length = BOOST_PP_CAT(Process, name)(input, kZero); \
    RefCntBuffer result(length); \
    auto* pos = BOOST_PP_CAT(Process, name)(input, result.udata()); \
    DCHECK_EQ(result.uend(), pos); \
    return result; \
  } \
  size_t BOOST_PP_CAT(Serialize, name)(type input, size_t size) { \
    return BOOST_PP_CAT(Process, name)(input, size); \
  } \
  uint8_t* BOOST_PP_CAT(Serialize, name)(type input, uint8_t* pos) { \
    return BOOST_PP_CAT(Process, name)(input, pos); \
  }

#define REDIS_PRIMITIVES_DEFINE(r, data, elem) DO_REDIS_PRIMITIVES_DEFINE elem

BOOST_PP_SEQ_FOR_EACH(REDIS_PRIMITIVES_DEFINE, ~, REDIS_PRIMITIVES)

}  // namespace redisserver
}  // namespace yb
