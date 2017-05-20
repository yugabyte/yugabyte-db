//
// Copyright (c) YugaByte, Inc.
//
#include "yb/rpc/redis_rpc.h"

#include "yb/common/redis_protocol.pb.h"

#include "yb/rpc/messenger.h"
#include "yb/rpc/negotiation.h"
#include "yb/rpc/reactor.h"
#include "yb/rpc/redis_encoding.h"
#include "yb/rpc/rpc_introspection.pb.h"

#include "yb/util/logging.h"
#include "yb/util/size_literals.h"
#include "yb/util/split.h"

#include "yb/util/debug/trace_event.h"

#include "yb/util/memory/memory.h"

DECLARE_bool(rpc_dump_all_traces);
DECLARE_int32(rpc_slow_query_threshold_ms);

using strings::Substitute;
using std::placeholders::_1;

namespace yb {
namespace rpc {

namespace {

constexpr size_t kMaxBufferSize = 512_MB;
constexpr size_t kMaxNumberOfArgs = 1 << 20;
constexpr size_t kLineEndLength = 2;

} // namespace

// RedisParser is a finite state machine with memory.
// It could remember current parsing state and be invoked again when new data arrives.
// In this case parsing will be continued from the last position.
class RedisParser {
 public:
  explicit RedisParser(Slice source, std::vector<Slice>* args)
      : pos_(source.data()), end_(source.end()), args_(args)
  {}

  // Begin of input is going to be consumed, so we should adjust our pointers.
  // Since the beginning of input is being consumed by shifting the remaining bytes to the
  // beginning of the buffer.
  void Consume(size_t count) {
    pos_ -= count;
    end_ -= count;
    if (token_begin_ != nullptr) {
      token_begin_ -= count;
    }
  }

  // New data arrived, so update the end of available bytes.
  void Update(Slice source) {
    DCHECK_BETWEEN(pos_, source.data(), source.end());
    DCHECK_BETWEEN(end_, source.data(), source.end());

    end_ = source.end();
  }

  // Parse next command.
  CHECKED_STATUS NextCommand(const uint8_t** end_of_command) {
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

  CHECKED_STATUS AdvanceToNextToken() {
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

  CHECKED_STATUS Initial() {
    token_begin_ = pos_;
    state_ = *pos_ == '*' ? State::BULK_HEADER : State::SINGLE_LINE;
    return Status::OK();
  }

  CHECKED_STATUS SingleLine() {
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

  CHECKED_STATUS BulkHeader() {
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

  CHECKED_STATUS BulkArgumentSize() {
    auto status = FindEndOfLine();
    if (!status.ok() || incomplete_) {
      return status;
    }
    ptrdiff_t current_size;
    RETURN_NOT_OK(ParseNumber('$', 0, kMaxBufferSize, "Argument size", &current_size));
    state_ = State::BULK_ARGUMENT_BODY;
    token_begin_ = pos_;
    current_argument_size_ = current_size;
    return Status::OK();
  }

  CHECKED_STATUS BulkArgumentBody() {
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

  CHECKED_STATUS FindEndOfLine() {
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
  CHECKED_STATUS ParseNumber(char prefix,
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
    auto parsed_number = std::strtoll(pointer_cast<const char*>(token_begin_ + 1), &token_end, 10);
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
                  Substitute("$0 out of expected range [$1, $2] : $3",
                      name, min, max, parsed_number));
    *out = static_cast<ptrdiff_t>(parsed_number);
    return Status::OK();
  }

  // Current parsing position.
  const uint8_t* pos_;

  // End of data.
  const uint8_t* end_;

  // Command arguments.
  std::vector<Slice>* args_;

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

RedisConnectionContext::RedisConnectionContext() {}
RedisConnectionContext::~RedisConnectionContext() {}

void RedisConnectionContext::RunNegotiation(ConnectionPtr connection, const MonoTime& deadline) {
  Negotiation::RedisNegotiation(std::move(connection), deadline);
}

Status RedisConnectionContext::ProcessCalls(const ConnectionPtr& connection,
                                            Slice slice,
                                            size_t* consumed) {
  if (!parser_) {
    parser_.reset(new RedisParser(slice, /* args */ nullptr));
  } else {
    parser_->Update(slice);
  }
  RedisParser& parser = *parser_;
  *consumed = 0;
  const uint8_t* begin_of_command = slice.data();
  for(;;) {
    const uint8_t* end_of_command = nullptr;
    RETURN_NOT_OK(parser.NextCommand(&end_of_command));
    if (end_of_command == nullptr) {
      break;
    }
    RETURN_NOT_OK(HandleInboundCall(connection, Slice(begin_of_command, end_of_command)));
    begin_of_command = end_of_command;
    *consumed = end_of_command - slice.data();
  }
  parser.Consume(*consumed);
  return Status::OK();
}

Status RedisConnectionContext::HandleInboundCall(const ConnectionPtr& connection,
                                                 Slice redis_command) {
  auto reactor_thread = connection->reactor_thread();
  DCHECK(reactor_thread->IsCurrentThread());

  RedisInboundCall* call;
  InboundCallPtr call_ptr(call = new RedisInboundCall(connection, call_processed_listener()));

  Status s = call->ParseFrom(redis_command);
  if (!s.ok()) {
    return s;
  }

  Enqueue(std::move(call_ptr));

  return Status::OK();
}

size_t RedisConnectionContext::BufferLimit() {
  return kMaxBufferSize;
}

RedisInboundCall::RedisInboundCall(ConnectionPtr conn,
                                   CallProcessedListener call_processed_listener)
    : InboundCall(std::move(conn), std::move(call_processed_listener)) {
}

Status RedisInboundCall::ParseFrom(Slice source) {
  TRACE_EVENT_FLOW_BEGIN0("rpc", "RedisInboundCall", this);
  TRACE_EVENT0("rpc", "RedisInboundCall::ParseFrom");

  request_data_.assign(source.data(), source.end());
  serialized_request_ = source = Slice(request_data_.data(), request_data_.size());

  Status status;
  RedisParser parser(source, &client_command_.cmd_args);
  const uint8_t* end_of_command = nullptr;
  RETURN_NOT_OK(parser.NextCommand(&end_of_command));
  if (end_of_command != source.end()) {
    return STATUS_SUBSTITUTE(Corruption,
                             "Parsed size $0 does not match source size $1",
                             end_of_command - source.data(),
                             source.size());
  }

  remote_method_ = RemoteMethod("yb.redisserver.RedisServerService", "anyMethod");

  return Status::OK();
}

MonoTime RedisInboundCall::GetClientDeadline() const {
  return MonoTime::Max();  // No timeout specified in the protocol for Redis.
}

void RedisInboundCall::LogTrace() const {
  MonoTime now = MonoTime::Now(MonoTime::FINE);
  auto total_time = now.GetDeltaSince(timing_.time_received).ToMilliseconds();

  if (PREDICT_FALSE(FLAGS_rpc_dump_all_traces)) {
    LOG(INFO) << ToString() << " took " << total_time << "ms. Trace:";
    trace_->Dump(&LOG(INFO), /* include_time_deltas */ true);
  }
}

string RedisInboundCall::ToString() const {
  return strings::Substitute("Redis Call $0 from $1",
      remote_method_.ToString(),
      connection()->remote().ToString());
}

void RedisInboundCall::DumpPB(const DumpRunningRpcsRequestPB& req,
                              RpcCallInProgressPB* resp) {
  if (req.include_traces() && trace_) {
    resp->set_trace_buffer(trace_->DumpToString(true));
  }
  resp->set_micros_elapsed(MonoTime::Now(MonoTime::FINE).GetDeltaSince(timing_.time_received)
      .ToMicroseconds());
}

Status RedisInboundCall::SerializeResponseBuffer(const google::protobuf::MessageLite& response,
                                                 bool is_success) {
  if (!is_success) {
    const ErrorStatusPB& error_status = static_cast<const ErrorStatusPB&>(response);
    response_msg_buf_ = util::RefCntBuffer(EncodeAsError(error_status.message()));
    return Status::OK();
  }

  const RedisResponsePB& redis_response = static_cast<const RedisResponsePB&>(response);

  if (redis_response.code() == RedisResponsePB_RedisStatusCode_SERVER_ERROR) {
    response_msg_buf_ = util::RefCntBuffer(
        EncodeAsError("Request was unable to be processed from server."));
    return Status::OK();
  }

  // TODO(Amit): As and when we implement get/set and its h* equivalents, we would have to
  // handle arrays, hashes etc. For now, we only support the string response.

  if (redis_response.code() != RedisResponsePB_RedisStatusCode_OK) {
    // We send a nil response for all non-ok statuses as of now.
    // TODO: Follow redis error messages.
    response_msg_buf_ = util::RefCntBuffer(kNilResponse);
  } else {
    if (redis_response.has_string_response()) {
      response_msg_buf_ = util::RefCntBuffer(
          EncodeAsSimpleString(redis_response.string_response()));
    } else if (redis_response.has_int_response()) {
      response_msg_buf_ = util::RefCntBuffer(EncodeAsInteger(redis_response.int_response()));
    } else {
      response_msg_buf_ = util::RefCntBuffer(EncodeAsSimpleString("OK"));
    }
  }
  return Status::OK();
}

void RedisInboundCall::Serialize(std::deque<util::RefCntBuffer>* output) const {
  output->push_back(response_msg_buf_);
}

} // namespace rpc
} // namespace yb
