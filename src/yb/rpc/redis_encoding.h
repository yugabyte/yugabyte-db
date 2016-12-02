// Copyright (c) YugaByte, Inc.
#ifndef YB_RPC_REDIS_ENCODING_H_
#define YB_RPC_REDIS_ENCODING_H_

#include <string>

#include "yb/gutil/strings/join.h"

namespace yb {
namespace rpc {

const char kNilResponse[] = "$-1\r\n";

// Encode the given input string as a integer string (eg "123"). Integer(s) are formatted as
// :<Integer>\r\n
// For more info: http://redis.io/topics/protocol
string EncodeAsInteger(string input);

// Encode the given input string as a integer string. Integer(s) are formatted as
// :<String representation of Integer>\r\n
// For more info: http://redis.io/topics/protocol
string EncodeAsInteger(int64_t number);

// Encode the given input string as a simple string. Simple string(s) are formatted as
// +<string>\r\n
// For more info: http://redis.io/topics/protocol
string EncodeAsSimpleString(string input);

// Encode the given message string as an error message. Error messages(s) are formatted as
// -<message>\r\n
// For more info: http://redis.io/topics/protocol
string EncodeAsError(string message);

// Encode the given input string as a bulk string. Bulk string(s) are formatted as
// $<length>\r\n<string data>\r\n
// For more info: http://redis.io/topics/protocol
string EncodeAsBulkString(string input);

// Encode the vector of encoded elementes into a multi-bulk-array. Bulk array(s) are formatted as
// *<num-elements>\r\n<encoded data terminating in \r\n> ... <encoded data terminating in \r\n>
// For more info: http://redis.io/topics/protocol
string EncodeAsArrays(vector<string> encoded_elements);

}  // namespace rpc
}  // namespace yb

#endif  // YB_RPC_REDIS_ENCODING_H_
