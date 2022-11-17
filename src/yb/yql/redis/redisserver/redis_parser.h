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

#include <memory>
#include <string>

#include <boost/container/small_vector.hpp>

#include "yb/client/client_fwd.h"

#include "yb/util/status_fwd.h"
#include "yb/util/net/socket.h"
#include "yb/util/size_literals.h"

#include "yb/yql/redis/redisserver/redis_fwd.h"

namespace yb {
namespace redisserver {

constexpr size_t kMaxRedisValueSize = 512_MB;
constexpr int64_t kNoneTtl = -1;

Status ParseSet(client::YBRedisWriteOp *op, const RedisClientCommand& args);
Status ParseGet(client::YBRedisReadOp* op, const RedisClientCommand& args);

// TODO: make additional command support here

// RedisParser is a finite state machine with memory.
// It could remember current parsing state and be invoked again when new data arrives.
// In this case parsing will be continued from the last position.
class RedisParser {
 public:
  explicit RedisParser(const IoVecs& source)
      : source_(source), full_size_(IoVecsFullSize(source)) {
  }

  void SetArgs(boost::container::small_vector_base<Slice>* args);

  // Begin of input is going to be consumed, so we should adjust our pointers.
  // Since the beginning of input is being consumed by shifting the remaining bytes to the
  // beginning of the buffer.
  void Consume(size_t count);

  // New data arrived, so update the end of available bytes.
  void Update(const IoVecs& source);

  // Parse next command.
  Result<size_t> NextCommand();

 private:
  // Redis command could be of 2 types.
  // First one is just single line that terminates with \r\n.
  // Second one is bulk command, that has form:
  // *<BULK_ARGUMENTS>\r\n
  // $<SIZE_IN_BYTES_OF_BULK_ARGUMENT_1>\r\n
  // <BULK_ARGUMENT_1>\r\n
  // ...
  // $<SIZE_IN_BYTES_OF_BULK_ARGUMENT_N>\r\n
  // <BULK_ARGUMENT_N>\r\n
  enum class State {
    // Initial state of parser.
    INITIAL,
    // We determined that command is single line command and waiting for \r\n
    SINGLE_LINE,
    // We are parsing the first line of a bulk command.
    BULK_HEADER,
    // We are parsing bulk argument size. arguments_left_ has valid value.
    BULK_ARGUMENT_SIZE,
    // We are parsing bulk argument body. arguments_left_, current_argument_size_ have valid values.
    BULK_ARGUMENT_BODY,
    // Just mark that we finished parsing of command. Will become INITIAL in NextCommand.
    FINISHED,
  };

  Status AdvanceToNextToken();
  Status Initial();
  Status SingleLine();
  Status BulkHeader();
  Status BulkArgumentSize();
  Status BulkArgumentBody();
  Status FindEndOfLine();

  // Parses number with specified bounds.
  // Number is located in separate line, and contain prefix before actual number.
  // Line starts at token_begin_ and pos_ is a start of next line.
  Result<ptrdiff_t> ParseNumber(char prefix,
                                ptrdiff_t min,
                                ptrdiff_t max,
                                const char* name);

  // Returns pointer to byte with specified offset in all iovecs of source_.
  // Pointer byte is valid, the end of valid range should be determined separately if required.
  const char* offset_to_pointer(size_t offset) const;

  std::pair<size_t, size_t> offset_to_idx_and_local_offset(size_t offset) const;

  char char_at_offset(size_t offset) const {
    return *offset_to_pointer(offset);
  }

  static constexpr size_t kNoToken = std::numeric_limits<size_t>::max();

  // Data to parse.
  IoVecs source_;

  size_t full_size_;

  // Current parsing position.
  size_t pos_ = 0;

  // Command arguments.
  boost::container::small_vector_base<Slice>* args_ = nullptr;

  // Parser state.
  State state_ = State::INITIAL;

  // Beginning of last token.
  size_t token_begin_ = kNoToken;

  // Mark that current token is incomplete.
  bool incomplete_ = false;

  // Number of arguments left in bulk command.
  size_t arguments_left_ = 0;

  // Size of the current argument in bulk command.
  size_t current_argument_size_ = 0;

  std::vector<char> number_buffer_;
};

}  // namespace redisserver
}  // namespace yb
