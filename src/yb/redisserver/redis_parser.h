// Copyright (c) YugaByte, Inc.
#ifndef YB_REDISSERVER_REDIS_PARSER_H_
#define YB_REDISSERVER_REDIS_PARSER_H_

#include <memory>
#include <string>

#include "yb/client/async_rpc.h"
#include "yb/client/callbacks.h"
#include "yb/client/client.h"
#include "yb/client/client_builder-internal.h"

#include "yb/redisserver/redis_fwd.h"

#include "yb/util/slice.h"
#include "yb/util/status.h"
#include "yb/util/size_literals.h"

namespace yb {
namespace redisserver {

constexpr size_t kMaxBufferSize = 512_MB;
constexpr int64_t kNoneTtl = -1;

CHECKED_STATUS ParseSet(yb::client::YBRedisWriteOp *op, const RedisClientCommand& args);
CHECKED_STATUS ParseMSet(yb::client::YBRedisWriteOp *op, const RedisClientCommand& args);
CHECKED_STATUS ParseHSet(yb::client::YBRedisWriteOp *op, const RedisClientCommand& args);
CHECKED_STATUS ParseHMSet(yb::client::YBRedisWriteOp *op, const RedisClientCommand& args);
CHECKED_STATUS ParseSAdd(yb::client::YBRedisWriteOp *op, const RedisClientCommand& args);
CHECKED_STATUS ParseGetSet(yb::client::YBRedisWriteOp *op, const RedisClientCommand& args);
CHECKED_STATUS ParseAppend(yb::client::YBRedisWriteOp* op, const RedisClientCommand& args);
CHECKED_STATUS ParseDel(yb::client::YBRedisWriteOp* op, const RedisClientCommand& args);
CHECKED_STATUS ParseSetRange(yb::client::YBRedisWriteOp* op, const RedisClientCommand& args);
CHECKED_STATUS ParseIncr(yb::client::YBRedisWriteOp* op, const RedisClientCommand& args);

CHECKED_STATUS ParseGet(yb::client::YBRedisReadOp* op, const RedisClientCommand& args);
CHECKED_STATUS ParseMGet(yb::client::YBRedisReadOp* op, const RedisClientCommand& args);
CHECKED_STATUS ParseHGet(yb::client::YBRedisReadOp *op, const RedisClientCommand& args);
CHECKED_STATUS ParseHMGet(yb::client::YBRedisReadOp* op, const RedisClientCommand& args);
CHECKED_STATUS ParseHGetAll(yb::client::YBRedisReadOp* op, const RedisClientCommand& args);
CHECKED_STATUS ParseSMembers(yb::client::YBRedisReadOp* op, const RedisClientCommand& args);
CHECKED_STATUS ParseStrLen(yb::client::YBRedisReadOp* op, const RedisClientCommand& args);
CHECKED_STATUS ParseExists(yb::client::YBRedisReadOp* op, const RedisClientCommand& args);
CHECKED_STATUS ParseGetRange(yb::client::YBRedisReadOp* op, const RedisClientCommand& args);

// TODO: make additional command support here

// RedisParser is a finite state machine with memory.
// It could remember current parsing state and be invoked again when new data arrives.
// In this case parsing will be continued from the last position.
class RedisParser {
 public:
  explicit RedisParser(Slice source)
      : begin_(source.data()), end_(source.end()), pos_(begin_)
  {}

  void SetArgs(boost::container::small_vector_base<Slice>* args) {
    args_ = args;
  }

  // Begin of input is going to be consumed, so we should adjust our pointers.
  // Since the beginning of input is being consumed by shifting the remaining bytes to the
  // beginning of the buffer.
  void Consume(size_t count);

  // New data arrived, so update the end of available bytes.
  void Update(Slice source);

  // Parse next command.
  CHECKED_STATUS NextCommand(const uint8_t** end_of_command);
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

  CHECKED_STATUS AdvanceToNextToken();
  CHECKED_STATUS Initial();
  CHECKED_STATUS SingleLine();
  CHECKED_STATUS BulkHeader();
  CHECKED_STATUS BulkArgumentSize();
  CHECKED_STATUS BulkArgumentBody();
  CHECKED_STATUS FindEndOfLine();

  // Parses number with specified bounds.
  // Number is located in separate line, and contain prefix before actual number.
  // Line starts at token_begin_ and pos_ is a start of next line.
  CHECKED_STATUS ParseNumber(char prefix,
                             ptrdiff_t min,
                             ptrdiff_t max,
                             const char* name,
                             ptrdiff_t* out);

  // Begin of data.
  const uint8_t* begin_;

  // End of data.
  const uint8_t* end_;

  // Current parsing position.
  const uint8_t* pos_;

  // Command arguments.
  boost::container::small_vector_base<Slice>* args_ = nullptr;

  // Parser state.
  State state_ = State::INITIAL;

  // Beginning of last token.
  const uint8_t* token_begin_ = nullptr;

  // Mark that current token is incomplete.
  bool incomplete_ = false;

  // Number of arguments left in bulk command.
  size_t arguments_left_ = 0;

  // Size of the current argument in bulk command.
  size_t current_argument_size_ = 0;
};

}  // namespace redisserver
}  // namespace yb

#endif  // YB_REDISSERVER_REDIS_PARSER_H_
