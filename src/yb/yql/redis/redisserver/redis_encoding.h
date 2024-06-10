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

#include <stdint.h>

#include <string>

#include <boost/preprocessor/seq/for_each.hpp>

namespace google {
namespace protobuf {
template <typename Element> class RepeatedPtrField;
}
}

namespace yb {

class RefCntBuffer;

namespace redisserver {

extern const std::string kNilResponse;
extern const std::string kOkResponse;
extern const std::string kInfoResponse;

// Integer:
// Encode the given input string as a integer string (eg "123"). Integer(s) are formatted as
// :<Integer>\r\n
// For more info: http://redis.io/topics/protocol

// SimpleString:
// Encode the given input string as a simple string. Simple string(s) are formatted as
// +<string>\r\n
// For more info: http://redis.io/topics/protocol

// Error:
// Encode the given message string as an error message. Error messages(s) are formatted as
// -<message>\r\n
// For more info: http://redis.io/topics/protocol

// BulkString:
// Encode the given input string as a bulk string. Bulk string(s) are formatted as
// $<length>\r\n<string data>\r\n
// For more info: http://redis.io/topics/protocol

// Array:
// Encode the vector of encoded elementes into a multi-bulk-array. Bulk array(s) are formatted as
// *<num-elements>\r\n<encoded data terminating in \r\n> ... <encoded data terminating in \r\n>
// For more info: http://redis.io/topics/protocol

#define REDIS_PRIMITIVES \
  ((Integer, const std::string&)) \
  ((Integer, int64_t)) \
  ((SimpleString, const std::string&)) \
  ((BulkString, const std::string&)) \
  ((Error, const std::string&)) \
  ((Array, const google::protobuf::RepeatedPtrField<std::string>&)) \
  ((Array, const std::initializer_list<std::string>&)) \
  ((Encoded, const std::string&)) \
  ((EncodedArray, const google::protobuf::RepeatedPtrField<std::string>&)) \
  /**/

#define DO_REDIS_PRIMITIVES_FORWARD(name, type) \
  RefCntBuffer BOOST_PP_CAT(EncodeAs, name)(type input); \
  size_t BOOST_PP_CAT(Serialize, name)(type input, size_t size); \
  uint8_t* BOOST_PP_CAT(Serialize, name)(type input, uint8_t* pos);

#define REDIS_PRIMITIVES_FORWARD(r, data, elem) DO_REDIS_PRIMITIVES_FORWARD elem

BOOST_PP_SEQ_FOR_EACH(REDIS_PRIMITIVES_FORWARD, ~, REDIS_PRIMITIVES)

template <typename Container>
std::string EncodeAsArrayOfEncodedElements(const Container& encoded_elements) {
  return StrCat("*", encoded_elements.size(), "\r\n", JoinStrings(encoded_elements, ""));
}

}  // namespace redisserver
}  // namespace yb
