// Copyright (c) YugaByte, Inc.
#ifndef YB_REDISSERVER_REDIS_ENCODING_H
#define YB_REDISSERVER_REDIS_ENCODING_H

#include <string>

#include "yb/gutil/strings/join.h"

namespace yb {
namespace redisserver {

const char kNilResponse[] = "$-1\r\n";

// Encode the given input string as a integer string (eg "123"). Integer(s) are formatted as
// :<Integer>\r\n
// For more info: http://redis.io/topics/protocol
string EncodeAsInteger(const std::string& input);

// Encode the given input string as a integer string. Integer(s) are formatted as
// :<String representation of Integer>\r\n
// For more info: http://redis.io/topics/protocol
string EncodeAsInteger(int64_t number);

// Encode the given input string as a simple string. Simple string(s) are formatted as
// +<string>\r\n
// For more info: http://redis.io/topics/protocol
string EncodeAsSimpleString(const std::string& input);

// Encode the given message string as an error message. Error messages(s) are formatted as
// -<message>\r\n
// For more info: http://redis.io/topics/protocol
string EncodeAsError(const std::string& message);

// Encode the given input string as a bulk string. Bulk string(s) are formatted as
// $<length>\r\n<string data>\r\n
// For more info: http://redis.io/topics/protocol
string EncodeAsBulkString(const std::string& input);

// Encode the vector of encoded elementes into a multi-bulk-array. Bulk array(s) are formatted as
// *<num-elements>\r\n<encoded data terminating in \r\n> ... <encoded data terminating in \r\n>
// For more info: http://redis.io/topics/protocol
string EncodeAsArrays(const std::vector<std::string>& encoded_elements);

}  // namespace redisserver
}  // namespace yb

#endif  // YB_REDISSERVER_REDIS_ENCODING_H
