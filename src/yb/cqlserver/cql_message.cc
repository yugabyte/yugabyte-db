// Copyright (c) YugaByte, Inc.

#include "yb/cqlserver/cql_message.h"
#include "yb/gutil/endian.h"
#include "yb/gutil/strings/substitute.h"

namespace yb {
namespace cqlserver {

using std::shared_ptr;
using std::unique_ptr;
using std::string;
using std::set;
using std::vector;
using std::unordered_map;
using strings::Substitute;

#define RETURN_NOT_ENOUGH(sz)                               \
  do {                                                      \
    if (body_.size() < (sz)) {                              \
      return STATUS(NetworkError, "Truncated CQL message"); \
    }                                                       \
  } while (0)

// ------------------------------------ CQL request -----------------------------------
bool CQLRequest::ParseRequest(
    const Slice& mesg, unique_ptr<CQLRequest>* request, unique_ptr<CQLResponse>* error_response) {

  *request = nullptr;
  *error_response = nullptr;

// Short-hand macros for parsing fields in the message header
#define PARSE_BYTE(buf, pos, type)  static_cast<type>(buf[pos])
#define PARSE_SHORT(buf, pos, type) static_cast<type>(NetworkByteOrder::Load16(&buf[pos]))
#define PARSE_INT(buf, pos, type)   static_cast<type>(NetworkByteOrder::Load32(&buf[pos]))
#define PARSE_LONG(buf, pos, type)  static_cast<type>(NetworkByteOrder::Load64(&buf[pos]))

  if (mesg.size() < kMessageHeaderLength) {
    error_response->reset(
        new ErrorResponse(
            static_cast<StreamId>(0), ErrorResponse::Code::PROTOCOL_ERROR, "Incomplete header"));
    return false;
  }

  Header header(
      PARSE_BYTE(mesg, kHeaderPosVersion, Version),
      PARSE_BYTE(mesg, kHeaderPosFlags, Flags),
      PARSE_SHORT(mesg, kHeaderPosStreamId, StreamId),
      PARSE_BYTE(mesg, kHeaderPosOpcode, Opcode));
  uint32_t length = PARSE_INT(mesg, kHeaderPosLength, uint32_t);
  DVLOG(4) << "CQL message "
           << "version 0x" << std::hex << static_cast<uint32_t>(header.version)   << " "
           << "flags 0x"   << std::hex << static_cast<uint32_t>(header.flags)     << " "
           << "stream id " << std::dec << static_cast<uint32_t>(header.stream_id) << " "
           << "opcode 0x"  << std::hex << static_cast<uint32_t>(header.opcode)    << " "
           << "length "    << std::dec << static_cast<uint32_t>(length);

#undef PARSE_BYTE
#undef PARSE_SHORT
#undef PARSE_INT
#undef PARSE_LONG

  // Verify proper version that the response bit is not set in the request protocol version and
  // the protocol version is one we support.
  if (header.version & kResponseVersion) {
    error_response->reset(
        new ErrorResponse(
            header.stream_id, ErrorResponse::Code::PROTOCOL_ERROR, "Not a request"));
    return false;
  }
  if (header.version < kMinimumVersion || header.version > kCurrentVersion) {
    error_response->reset(
        new ErrorResponse(
            header.stream_id, ErrorResponse::Code::PROTOCOL_ERROR,
            Substitute("Protocol version $0 not supported. Supported versions are between "
                "$1 and $2.", header.version, kMinimumVersion, kCurrentVersion)));
    return false;
  }

  const Slice body =
      (mesg.size() == kMessageHeaderLength) ?
      Slice() : Slice(&mesg[kMessageHeaderLength], mesg.size() - kMessageHeaderLength);

  // Construct the skeleton request by the opcode
  switch (header.opcode) {
    case Opcode::STARTUP:
      request->reset(new StartupRequest(header, body));
      break;
    case Opcode::AUTH_RESPONSE:
      request->reset(new AuthResponseRequest(header, body));
      break;
    case Opcode::OPTIONS:
      request->reset(new OptionsRequest(header, body));
      break;
    case Opcode::QUERY:
      request->reset(new QueryRequest(header, body));
      break;
    case Opcode::PREPARE:
      request->reset(new PrepareRequest(header, body));
      break;
    case Opcode::EXECUTE:
      request->reset(new ExecuteRequest(header, body));
      break;
    case Opcode::BATCH:
      request->reset(new BatchRequest(header, body));
      break;
    case Opcode::REGISTER:
      request->reset(new RegisterRequest(header, body));
      break;

    // These are not request but response opcodes
    case Opcode::ERROR:
    case Opcode::READY:
    case Opcode::AUTHENTICATE:
    case Opcode::SUPPORTED:
    case Opcode::RESULT:
    case Opcode::EVENT:
    case Opcode::AUTH_CHALLENGE:
    case Opcode::AUTH_SUCCESS:
      error_response->reset(
          new ErrorResponse(
              header.stream_id, ErrorResponse::Code::PROTOCOL_ERROR, "Not a request opcode"));
      return false;

    // default: -> fall through
  }

  if (*request == nullptr) {
    error_response->reset(
        new ErrorResponse(
            header.stream_id, ErrorResponse::Code::PROTOCOL_ERROR, "Unknown opcode"));
    return false;
  }

  // Parse the request body
  const Status status = request->get()->ParseBody();
  if (!status.ok()) {
    error_response->reset(
        new ErrorResponse(
            request->get(), ErrorResponse::Code::PROTOCOL_ERROR, status.message().ToString()));
  } else if (!request->get()->body_.empty()) {
    // Flag error when there are bytes remaining after we have parsed the whole request body
    // according to the protocol. Either the request's length field from the client is
    // wrong. Or we could have a bug in our parser.
    error_response->reset(
        new ErrorResponse(
            request->get(), ErrorResponse::Code::PROTOCOL_ERROR, "Request length too long"));
  }

  // If there is any error, free the partially parsed request and return.
  if (*error_response != nullptr) {
    *request = nullptr;
    return false;
  }

  return true;
}

CQLRequest::CQLRequest(const Header& header, const Slice& body) : CQLMessage(header), body_(body) {
}

CQLRequest::~CQLRequest() {
}

//----------------------------------------------------------------------------------------

static inline const char* ToChar(const uint8_t* data) {
  return reinterpret_cast<const char*>(data);
}

static inline const char* ToChar(const in_addr_t* data) {
  return reinterpret_cast<const char*>(data);
}

// Short-hand macros for parsing number and bytes in the message body
#define LOAD_BYTE(ptr) (*ptr)

#define PARSE_NUMBER(type, sz, parser, val)         \
  do {                                              \
    RETURN_NOT_ENOUGH(sz);                          \
    *val = static_cast<type>(parser(body_.data())); \
    body_.remove_prefix(sz);                        \
  } while (0)

#define PARSE_BYTES(len_type, len_parser, val)                        \
  do {                                                                \
    len_type len;                                                     \
    RETURN_NOT_OK(len_parser(&len));                                  \
    RETURN_NOT_ENOUGH(len);                                           \
    *val = string(ToChar(body_.data()), len); \
    body_.remove_prefix(len);                                         \
  } while (0)

Status CQLRequest::ParseInt(int32_t* value) {
  PARSE_NUMBER(int32_t, kIntSize, NetworkByteOrder::Load32, value);
  DVLOG(4) << "CQL int " << *value;
  return Status::OK();
}

Status CQLRequest::ParseLong(int64_t* value) {
  PARSE_NUMBER(int64_t, kLongSize, NetworkByteOrder::Load64, value);
  DVLOG(4) << "CQL long " << *value;
  return Status::OK();
}

Status CQLRequest::ParseByte(uint8_t* value) {
  PARSE_NUMBER(uint8_t, kByteSize, LOAD_BYTE, value);
  DVLOG(4) << "CQL byte " << static_cast<uint32_t>(*value);
  return Status::OK();
}

Status CQLRequest::ParseShort(uint16_t* value) {
  PARSE_NUMBER(uint16_t, kShortSize, NetworkByteOrder::Load16, value);
  DVLOG(4) << "CQL short " << *value;
  return Status::OK();
}

Status CQLRequest::ParseString(string* value) {
  PARSE_BYTES(uint16_t, ParseShort, value);
  DVLOG(4) << "CQL string " << *value;
  return Status::OK();
}

Status CQLRequest::ParseLongString(string* value) {
  PARSE_BYTES(int32_t, ParseInt, value);
  DVLOG(4) << "CQL long string " << *value;
  return Status::OK();
}

Status CQLRequest::ParseUUID(string* value) {
  RETURN_NOT_ENOUGH(kUUIDSize);
  *value = string(ToChar(body_.data()), kUUIDSize);
  body_.remove_prefix(kUUIDSize);
  DVLOG(4) << "CQL uuid " << *value;
  return Status::OK();
}

Status CQLRequest::ParseStringList(vector<string>* list) {
  DVLOG(4) << "CQL string list ...";
  uint16_t count = 0;
  RETURN_NOT_OK(ParseShort(&count));
  for (uint16_t i = 0; i < count; ++i) {
    string value;
    RETURN_NOT_OK(ParseString(&value));
    list->push_back(value);
  }
  return Status::OK();
}

Status CQLRequest::ParseBytes(string* value) {
  PARSE_BYTES(int32_t, ParseInt, value);
  DVLOG(4) << "CQL bytes " << *value;
  return Status::OK();
}

Status CQLRequest::ParseShortBytes(string* value) {
  PARSE_BYTES(uint16_t, ParseShort, value);
  DVLOG(4) << "CQL short bytes " << *value;
  return Status::OK();
}

Status CQLRequest::ParseInet(Sockaddr* value) {
  string ipaddr;
  int32_t port = 0;
  PARSE_BYTES(uint8_t, ParseByte, &ipaddr);
  RETURN_NOT_OK(ParseInt(&port));
  // TODO(Robert): support IPv6
  if (ipaddr.size() != kIPv4Size) {
    return STATUS(NetworkError, "Implementation restriction: only IPv4 inet is supported");
  }
  if (port < 0 || port > 65535) {
    return STATUS(NetworkError, "Invalid inet port");
  }
  sockaddr_in addr;
  addr.sin_family = AF_INET;
  memcpy(&addr.sin_addr.s_addr, ipaddr.data(), ipaddr.size());
  addr.sin_port = htons(static_cast<uint16_t>(port));
  *value = addr;
  DVLOG(4) << "CQL inet " << value->ToString();
  return Status::OK();
}

Status CQLRequest::ParseConsistency(Consistency* value) {
  PARSE_NUMBER(Consistency, kConsistencySize, NetworkByteOrder::Load16, value);
  DVLOG(4) << "CQL consistency 0x" << std::hex << static_cast<uint32_t>(*value);
  return Status::OK();
}

Status CQLRequest::ParseStringMap(unordered_map<string, string>* map) {
  DVLOG(4) << "CQL string map ...";
  uint16_t count = 0;
  RETURN_NOT_OK(ParseShort(&count));
  for (uint16_t i = 0; i < count; ++i) {
    string name, value;
    RETURN_NOT_OK(ParseString(&name));
    RETURN_NOT_OK(ParseString(&value));
    (*map)[name] = value;
  }
  return Status::OK();
}

Status CQLRequest::ParseStringMultiMap(unordered_map<string, vector<string>>* map) {
  DVLOG(4) << "CQL string multimap ...";
  uint16_t count = 0;
  RETURN_NOT_OK(ParseShort(&count));
  for (uint16_t i = 0; i < count; ++i) {
    string name;
    vector<string> value;
    RETURN_NOT_OK(ParseString(&name));
    RETURN_NOT_OK(ParseStringList(&value));
    (*map)[name] = value;
  }
  return Status::OK();
}

Status CQLRequest::ParseBytesMap(unordered_map<string, string>* map) {
  DVLOG(4) << "CQL bytes map ...";
  uint16_t count = 0;
  RETURN_NOT_OK(ParseShort(&count));
  for (uint16_t i = 0; i < count; ++i) {
    string name, value;
    RETURN_NOT_OK(ParseString(&name));
    RETURN_NOT_OK(ParseBytes(&value));
    (*map)[name] = value;
  }
  return Status::OK();
}

Status CQLRequest::ParseValue(const bool with_name, Value* value) {
  DVLOG(4) << "CQL value ...";
  if (with_name) {
    RETURN_NOT_OK(ParseString(&value->name));
  }
  int32_t length;
  RETURN_NOT_OK(ParseInt(&length));
  if (length >= 0) {
    value->kind = Value::Kind::NOT_NULL;
    if (length > 0) {
      RETURN_NOT_ENOUGH(length);
      value->value = string(ToChar(body_.data()), length);
      body_.remove_prefix(length);
      DVLOG(4) << "CQL value bytes " << value->value;
    }
  } else if (version() >= kV4Version) {
    switch (length) {
      case -1:
        value->kind = Value::Kind::IS_NULL;
        break;
      case -2:
        value->kind = Value::Kind::NOT_SET;
        break;
      default:
        return STATUS(NetworkError, "Invalid length in value");
        break;
    }
  } else {
    value->kind = Value::Kind::IS_NULL;
  }
  return Status::OK();
}

Status CQLRequest::ParseQueryParameters(QueryParameters* params) {
  DVLOG(4) << "CQL query parameters ...";
  RETURN_NOT_OK(ParseConsistency(&params->consistency));
  RETURN_NOT_OK(ParseByte(&params->flags));
  if (params->flags & CQLMessage::QueryParameters::kWithValuesFlag) {
    const bool with_name = (params->flags & CQLMessage::QueryParameters::kWithNamesForValuesFlag);
    uint16_t count = 0;
    RETURN_NOT_OK(ParseShort(&count));
    for (uint16_t i = 0; i < count; ++i) {
      Value value;
      RETURN_NOT_OK(ParseValue(with_name, &value));
      params->values.push_back(value);
    }
  }
  if (params->flags & CQLMessage::QueryParameters::kWithPageSizeFlag) {
    RETURN_NOT_OK(ParseInt(&params->page_size));
  }
  if (params->flags & CQLMessage::QueryParameters::kWithPagingStateFlag) {
    RETURN_NOT_OK(ParseBytes(&params->paging_state));
  }
  if (params->flags & CQLMessage::QueryParameters::kWithSerialConsistencyFlag) {
    RETURN_NOT_OK(ParseConsistency(&params->serial_consistency));
  }
  if (params->flags & CQLMessage::QueryParameters::kWithDefaultTimestampFlag) {
    RETURN_NOT_OK(ParseLong(&params->default_timestamp));
  }
  return Status::OK();
}

#undef LOAD_BYTE
#undef PARSE_NUMBER
#undef PARSE_BYTES

// ------------------------------ Individual CQL requests -----------------------------------
StartupRequest::StartupRequest(const Header& header, const Slice& body)
    : CQLRequest(header, body) {
}

StartupRequest::~StartupRequest() {
}

Status StartupRequest::ParseBody() {
  return ParseStringMap(&options_);
}

CQLResponse* StartupRequest::Execute() {
  for (const auto& option : options_) {
    const auto& name = option.first;
    const auto& value = option.second;
    const auto it = SupportedResponse::options_.find(name);
    if (it == SupportedResponse::options_.end() ||
        std::find(it->second.begin(), it->second.end(), value) == it->second.end()) {
      return new ErrorResponse(
          this, ErrorResponse::Code::PROTOCOL_ERROR, "Unsupported option " + name);
    }
  }
  return new ReadyResponse(this);
}

//----------------------------------------------------------------------------------------
AuthResponseRequest::AuthResponseRequest(const Header& header, const Slice& body)
    : CQLRequest(header, body) {
}

AuthResponseRequest::~AuthResponseRequest() {
}

Status AuthResponseRequest::ParseBody() {
  return ParseBytes(&token_);
}

CQLResponse* AuthResponseRequest::Execute() {
  // TODO(Robert): authentication support
  return new ErrorResponse(this, ErrorResponse::Code::PROTOCOL_ERROR, "Not implemented yet");
}

//----------------------------------------------------------------------------------------
OptionsRequest::OptionsRequest(const Header& header, const Slice& body) : CQLRequest(header, body) {
}

OptionsRequest::~OptionsRequest() {
}

Status OptionsRequest::ParseBody() {
  // Options body is empty
  return Status::OK();
}

CQLResponse* OptionsRequest::Execute() {
  return new SupportedResponse(this);
}

//----------------------------------------------------------------------------------------
QueryRequest::QueryRequest(const Header& header, const Slice& body) : CQLRequest(header, body) {
}

QueryRequest::~QueryRequest() {
}

Status QueryRequest::ParseBody() {
  RETURN_NOT_OK(ParseLongString(&query_));
  RETURN_NOT_OK(ParseQueryParameters(&params_));
  return Status::OK();
}

CQLResponse* QueryRequest::Execute() {
  // TODO(Robert): return actual results
  return new VoidResultResponse(this);
}

//----------------------------------------------------------------------------------------
PrepareRequest::PrepareRequest(const Header& header, const Slice& body) : CQLRequest(header, body) {
}

PrepareRequest::~PrepareRequest() {
}

Status PrepareRequest::ParseBody() {
  RETURN_NOT_OK(ParseLongString(&query_));
  return Status::OK();
}

CQLResponse* PrepareRequest::Execute() {
  // TODO(Robert): return actual results
  return new ErrorResponse(this, ErrorResponse::Code::PROTOCOL_ERROR, "Not implemented yet");
}

//----------------------------------------------------------------------------------------
ExecuteRequest::ExecuteRequest(const Header& header, const Slice& body) : CQLRequest(header, body) {
}

ExecuteRequest::~ExecuteRequest() {
}

Status ExecuteRequest::ParseBody() {
  RETURN_NOT_OK(ParseShortBytes(&query_id_));
  RETURN_NOT_OK(ParseQueryParameters(&params_));
  return Status::OK();
}

CQLResponse* ExecuteRequest::Execute() {
  // TODO(Robert): return actual results
  return new ErrorResponse(this, ErrorResponse::Code::PROTOCOL_ERROR, "Not implemented yet");
}

//----------------------------------------------------------------------------------------
BatchRequest::BatchRequest(const Header& header, const Slice& body) : CQLRequest(header, body) {
}

BatchRequest::~BatchRequest() {
}

Status BatchRequest::ParseBody() {
  uint8_t type = 0;
  RETURN_NOT_OK(ParseByte(&type));
  type_ = static_cast<Type>(type);
  uint16_t query_count = 0;
  RETURN_NOT_OK(ParseShort(&query_count));
  for (uint16_t i = 0; i < query_count; ++i) {
    Query query;
    uint8_t is_prepared_query;
    RETURN_NOT_OK(ParseByte(&is_prepared_query));
    switch (is_prepared_query) {
      case 0:
        query.is_prepared = false;
        RETURN_NOT_OK(ParseLongString(&query.query));
        break;
      case 1:
        query.is_prepared = true;
        RETURN_NOT_OK(ParseShortBytes(&query.query_id));
        break;
      default:
        return STATUS(NetworkError, "Invalid is_prepared_query byte in batch request");
        break;
    }
    uint16_t value_count = 0;
    RETURN_NOT_OK(ParseShort(&value_count));
    for (uint16_t j = 0; i < value_count; ++j) {
      Value value;
      // with_name is not possible in the protocol due to a design flaw. See JIRA CASSANDRA-10246.
      RETURN_NOT_OK(ParseValue(false /* with_name */, &value));
      query.values.push_back(value);
    }
    queries_.push_back(query);
  }
  RETURN_NOT_OK(ParseConsistency(&consistency_));
  RETURN_NOT_OK(ParseByte(&flags_));
  if (flags_ & CQLMessage::QueryParameters::kWithSerialConsistencyFlag) {
    RETURN_NOT_OK(ParseConsistency(&serial_consistency_));
  }
  if (flags_ & CQLMessage::QueryParameters::kWithDefaultTimestampFlag) {
    RETURN_NOT_OK(ParseLong(&default_timestamp_));
  }
  return Status::OK();
}

CQLResponse* BatchRequest::Execute() {
  // TODO(Robert)
  return new ErrorResponse(this, ErrorResponse::Code::PROTOCOL_ERROR, "Not implemented yet");
}

//----------------------------------------------------------------------------------------
RegisterRequest::RegisterRequest(const Header& header, const Slice& body)
    : CQLRequest(header, body) {
}

RegisterRequest::~RegisterRequest() {
}

Status RegisterRequest::ParseBody() {
  return ParseStringList(&event_types_);
}

CQLResponse* RegisterRequest::Execute() {
  // TODO(Robert): implement real event responses
  return new ReadyResponse(this);
}

// ------------------------------------ CQL response -----------------------------------
CQLResponse::CQLResponse(const CQLRequest* request, const Opcode opcode)
    : CQLMessage(
          Header(
              request->version() | kResponseVersion, request->flags(), request->stream_id(),
              opcode)) {
}

CQLResponse::CQLResponse(const StreamId stream_id, const Opcode opcode)
    : CQLMessage(Header(kCurrentVersion | kResponseVersion, 0, stream_id, opcode)) {
}

CQLResponse::~CQLResponse() {
}

// Short-hand macros for serializing fields from the message header
#define SERIALIZE_BYTE(buf, pos, value) \
  do { (*buf)[pos] = static_cast<uint8_t>(value); } while (0)
#define SERIALIZE_SHORT(buf, pos, value) \
  NetworkByteOrder::Store16(&(*buf)[pos], static_cast<uint16_t>(value))
#define SERIALIZE_INT(buf, pos, value) \
  NetworkByteOrder::Store32(&(*buf)[pos], static_cast<int32_t>(value))
#define SERIALIZE_LONG(buf, pos, value) \
  NetworkByteOrder::Store64(&(*buf)[pos], static_cast<int64_t>(value))

void CQLResponse::Serialize(faststring* mesg) {
  SerializeHeader(mesg);
  SerializeBody(mesg);
  SERIALIZE_INT(mesg, kHeaderPosLength, mesg->size() - kMessageHeaderLength);
}

void CQLResponse::SerializeHeader(faststring* mesg) {
  mesg->resize(kMessageHeaderLength);
  SERIALIZE_BYTE(mesg, kHeaderPosVersion, version());
  SERIALIZE_BYTE(mesg, kHeaderPosFlags, flags());
  SERIALIZE_SHORT(mesg, kHeaderPosStreamId, stream_id());
  SERIALIZE_INT(mesg, kHeaderPosLength, 0);
  SERIALIZE_BYTE(mesg, kHeaderPosOpcode, opcode());
}

#undef SERIALIZE_BYTE
#undef SERIALIZE_SHORT
#undef SERIALIZE_INT
#undef SERIALIZE_LONG

// Short-hand macros for serializing number and bytes in the message body
#define STORE_BYTE(dst, src) *dst = src

#define SERIALIZE_NUMBER(type, sz, serializer, val, buf)    \
  do {                                                      \
    size_t curr_size = buf->size();                         \
    buf->resize(curr_size + sz);                            \
    serializer(&(*buf)[curr_size], static_cast<type>(val)); \
  } while (0)

#define SERIALIZE_BYTES(len_serializer, val, buf) \
  do {                                            \
    len_serializer(val.size(), buf);              \
    buf->append(val);                             \
  } while (0)

void CQLResponse::SerializeInt(const int32_t value, faststring* mesg) {
  SERIALIZE_NUMBER(int32_t, kIntSize, NetworkByteOrder::Store32, value, mesg);
}

void CQLResponse::SerializeLong(const int64_t value, faststring* mesg) {
  SERIALIZE_NUMBER(int64_t, kLongSize, NetworkByteOrder::Store64, value, mesg);
}

void CQLResponse::SerializeByte(const uint8_t value, faststring* mesg) {
  SERIALIZE_NUMBER(uint8_t, kByteSize, STORE_BYTE, value, mesg);
}

void CQLResponse::SerializeShort(const uint16_t value, faststring* mesg) {
  SERIALIZE_NUMBER(uint16_t, kShortSize, NetworkByteOrder::Store16, value, mesg);
}

void CQLResponse::SerializeString(const string& value, faststring* mesg) {
  SERIALIZE_BYTES(SerializeShort, value, mesg);
}

void CQLResponse::SerializeLongString(const string& value, faststring* mesg) {
  SERIALIZE_BYTES(SerializeInt, value, mesg);
}

void CQLResponse::SerializeUUID(const string& value, faststring* mesg) {
  if (value.size() == kUUIDSize) {
    mesg->append(value);
  } else {
    LOG(ERROR) << "Internal error: inconsistent UUID size: " << value.size();
    size_t curr_size = mesg->size();
    mesg->resize(curr_size + kUUIDSize);
    memset(&(*mesg)[curr_size], 0, kUUIDSize);
  }
}

void CQLResponse::SerializeStringList(const vector<string>& list, faststring* mesg) {
  SerializeShort(list.size(), mesg);
  for (int i = 0; i < list.size(); ++i) {
    SerializeString(list[i], mesg);
  }
}

void CQLResponse::SerializeBytes(const string& value, faststring* mesg) {
  SERIALIZE_BYTES(SerializeInt, value, mesg);
}

void CQLResponse::SerializeShortBytes(const string& value, faststring* mesg) {
  SERIALIZE_BYTES(SerializeShort, value, mesg);
}

void CQLResponse::SerializeInet(const Sockaddr& value, faststring* mesg) {
  // TODO(Robert): support IPv6
  SerializeByte(kIPv4Size, mesg);
  const auto& addr = value.addr();
  mesg->append(ToChar(&addr.sin_addr.s_addr), kIPv4Size);
  const uint16_t port = ntohs(addr.sin_port);
  SerializeInt(port, mesg);
}

void CQLResponse::SerializeConsistency(const Consistency value, faststring* mesg) {
  SERIALIZE_NUMBER(uint16_t, kConsistencySize, NetworkByteOrder::Store16, value, mesg);
}

void CQLResponse::SerializeStringMap(const unordered_map<string, string>& map, faststring* mesg) {
  SerializeShort(map.size(), mesg);
  for (const auto& element : map) {
    SerializeString(element.first, mesg);
    SerializeString(element.second, mesg);
  }
}

void CQLResponse::SerializeStringMultiMap(
    const unordered_map<string, vector<string>>& map, faststring* mesg) {
  SerializeShort(map.size(), mesg);
  for (const auto& element : map) {
    SerializeString(element.first, mesg);
    SerializeStringList(element.second, mesg);
  }
}

void CQLResponse::SerializeBytesMap(const unordered_map<string, string>& map, faststring* mesg) {
  SerializeShort(map.size(), mesg);
  for (const auto& element : map) {
    SerializeString(element.first, mesg);
    SerializeBytes(element.second, mesg);
  }
}

void CQLResponse::SerializeValue(const Value& value, faststring* mesg) {
  switch (value.kind) {
    case Value::Kind::NOT_NULL:
      SerializeInt(value.value.size(), mesg);
      mesg->append(value.value);
      return;
    case Value::Kind::IS_NULL:
      SerializeInt(-1, mesg);
      return;
    case Value::Kind::NOT_SET: // NOT_SET value kind should appear in request messages only.
      break;
    // default: fall through
  }
  LOG(ERROR) << "Internal error: invalid/unknown value kind " << static_cast<uint32_t>(value.kind);
  SerializeInt(-1, mesg);
}

#undef STORE_BYTE
#undef SERIALIZE_NUMBER
#undef SERIALIZE_BYTES

// ------------------------------ Individual CQL responses -----------------------------------
ErrorResponse::ErrorResponse(const CQLRequest* request, const Code code, const string& message)
    : CQLResponse(request, Opcode::ERROR), code_(code), message_(message) {
}

ErrorResponse::ErrorResponse(const CQLRequest* request, const Code code, const Status& status)
    : CQLResponse(request, Opcode::ERROR), code_(code),
      message_(ToChar(status.message().data())) {
}

ErrorResponse::ErrorResponse(const StreamId stream_id, const Code code, const string& message)
    : CQLResponse(stream_id, Opcode::ERROR), code_(code), message_(message) {
}

ErrorResponse::~ErrorResponse() {
}

void ErrorResponse::SerializeBody(faststring* mesg) {
  SerializeInt(static_cast<int32_t>(code_), mesg);
  SerializeString(message_, mesg);
}

//----------------------------------------------------------------------------------------
ReadyResponse::ReadyResponse(const CQLRequest* request) : CQLResponse(request, Opcode::READY) {
}

ReadyResponse::~ReadyResponse() {
}

void ReadyResponse::SerializeBody(faststring* mesg) {
  // Ready body is empty
}

//----------------------------------------------------------------------------------------
AuthenticateResponse::AuthenticateResponse(const CQLRequest* request, const string& authenticator)
    : CQLResponse(request, Opcode::AUTHENTICATE), authenticator_(authenticator) {
}

AuthenticateResponse::~AuthenticateResponse() {
}

void AuthenticateResponse::SerializeBody(faststring* mesg) {
  SerializeString(authenticator_, mesg);
}

//----------------------------------------------------------------------------------------
const unordered_map<string, vector<string>> SupportedResponse::options_ =
  { {"CQL_VERSION", {"3.0.0"} } };

SupportedResponse::SupportedResponse(const CQLRequest* request)
    : CQLResponse(request, Opcode::SUPPORTED) {
}

SupportedResponse::~SupportedResponse() {
}

void SupportedResponse::SerializeBody(faststring* mesg) {
  SerializeStringMultiMap(options_, mesg);
}

//----------------------------------------------------------------------------------------
ResultResponse::ResultResponse(const CQLRequest* request, const Kind kind)
  : CQLResponse(request, Opcode::RESULT), kind_(kind) {
}

ResultResponse::~ResultResponse() {
}

ResultResponse::RowsMetadata::Type::Type(const Id id) : id(id) {
  switch (id) {
    // Verify that the type id is a primitive type indeed.
    case Id::ASCII:
    case Id::BIGINT:
    case Id::BLOB:
    case Id::BOOLEAN:
    case Id::COUNTER:
    case Id::DECIMAL:
    case Id::DOUBLE:
    case Id::FLOAT:
    case Id::INT:
    case Id::TIMESTAMP:
    case Id::UUID:
    case Id::VARCHAR:
    case Id::VARINT:
    case Id::TIMEUUID:
    case Id::INET:
    case Id::DATE:
    case Id::TIME:
    case Id::SMALLINT:
    case Id::TINYINT:
      return;

    // Non-primitive types
    case Id::CUSTOM:
    case Id::LIST:
    case Id::MAP:
    case Id::SET:
    case Id::UDT:
    case Id::TUPLE:
      break;

    // default: fall through
  }

  LOG(ERROR) << "Internal error: invalid/unknown primitive type id " << static_cast<uint32_t>(id);
}

// These union members in Type below are not initialized by default. They need to be explicitly
// initialized using the new() operator in the Type constructors.
ResultResponse::RowsMetadata::Type::Type(const string& custom_class_name) : id(Id::CUSTOM) {
  new(&this->custom_class_name) string(custom_class_name);
}

ResultResponse::RowsMetadata::Type::Type(const Id id, shared_ptr<const Type> element_type)
    : id(id) {
  switch (id) {
    case Id::LIST:
    case Id::SET:
      new(&this->element_type) shared_ptr<const Type>(element_type);
      return;

    // Not list nor map
    case Id::CUSTOM:
    case Id::ASCII:
    case Id::BIGINT:
    case Id::BLOB:
    case Id::BOOLEAN:
    case Id::COUNTER:
    case Id::DECIMAL:
    case Id::DOUBLE:
    case Id::FLOAT:
    case Id::INT:
    case Id::TIMESTAMP:
    case Id::UUID:
    case Id::VARCHAR:
    case Id::VARINT:
    case Id::TIMEUUID:
    case Id::INET:
    case Id::DATE:
    case Id::TIME:
    case Id::SMALLINT:
    case Id::TINYINT:
    case Id::MAP:
    case Id::UDT:
    case Id::TUPLE:
      break;

    // default: fall through
  }

  LOG(ERROR) << "Internal error: invalid/unknown list/map type id " << static_cast<uint32_t>(id);
}

ResultResponse::RowsMetadata::Type::Type(shared_ptr<const MapType> map_type) : id(Id::MAP) {
  new(&this->map_type) shared_ptr<const MapType>(map_type);
}

ResultResponse::RowsMetadata::Type::Type(shared_ptr<const UDTType> udt_type) : id(Id::UDT) {
  new(&this->udt_type) shared_ptr<const UDTType>(udt_type);
}

ResultResponse::RowsMetadata::Type::Type(
    shared_ptr<const TupleComponentTypes> tuple_component_types) : id(Id::TUPLE) {
  new(&this->tuple_component_types) shared_ptr<const TupleComponentTypes>(tuple_component_types);
}

ResultResponse::RowsMetadata::Type::Type(const Type& t) : id(t.id) {
  switch (id) {
    case Id::CUSTOM:
      new(&this->custom_class_name) string(t.custom_class_name);
      return;
    case Id::ASCII:
    case Id::BIGINT:
    case Id::BLOB:
    case Id::BOOLEAN:
    case Id::COUNTER:
    case Id::DECIMAL:
    case Id::DOUBLE:
    case Id::FLOAT:
    case Id::INT:
    case Id::TIMESTAMP:
    case Id::UUID:
    case Id::VARCHAR:
    case Id::VARINT:
    case Id::TIMEUUID:
    case Id::INET:
    case Id::DATE:
    case Id::TIME:
    case Id::SMALLINT:
    case Id::TINYINT:
      return;
    case Id::LIST:
    case Id::SET:
      new(&element_type) shared_ptr<const Type>(t.element_type);
      return;
    case Id::MAP:
      new(&map_type) shared_ptr<const MapType>(t.map_type);
      return;
    case Id::UDT:
      new(&udt_type) shared_ptr<const UDTType>(t.udt_type);
      return;
    case Id::TUPLE:
      new(&tuple_component_types) shared_ptr<const TupleComponentTypes>(t.tuple_component_types);
      return;

    // default: fall through
  }

  LOG(ERROR) << "Internal error: unknown type id " << static_cast<uint32_t>(id);
}

ResultResponse::RowsMetadata::Type::~Type() {
  switch (id) {
    case Id::CUSTOM:
      custom_class_name.clear();
      return;
    case Id::ASCII:
    case Id::BIGINT:
    case Id::BLOB:
    case Id::BOOLEAN:
    case Id::COUNTER:
    case Id::DECIMAL:
    case Id::DOUBLE:
    case Id::FLOAT:
    case Id::INT:
    case Id::TIMESTAMP:
    case Id::UUID:
    case Id::VARCHAR:
    case Id::VARINT:
    case Id::TIMEUUID:
    case Id::INET:
    case Id::DATE:
    case Id::TIME:
    case Id::SMALLINT:
    case Id::TINYINT:
      return;
    case Id::LIST:
    case Id::SET:
      element_type.reset();
      return;
    case Id::MAP:
      map_type.reset();
      return;
    case Id::UDT:
      udt_type.reset();
      return;
    case Id::TUPLE:
      tuple_component_types.reset();
      return;

    // default: fall through
  }

  LOG(ERROR) << "Internal error: unknown type id " << static_cast<uint32_t>(id);
}

void ResultResponse::SerializeBody(faststring* mesg) {
  SerializeInt(static_cast<int32_t>(kind_), mesg);
  SerializeResultBody(mesg);
}

void ResultResponse::SerializeType(const RowsMetadata::Type* type, faststring* mesg) {
  SerializeShort(static_cast<uint16_t>(type->id), mesg);
  switch (type->id) {
    case RowsMetadata::Type::Id::CUSTOM:
      SerializeString(type->custom_class_name, mesg);
      return;
    case RowsMetadata::Type::Id::ASCII:
    case RowsMetadata::Type::Id::BIGINT:
    case RowsMetadata::Type::Id::BLOB:
    case RowsMetadata::Type::Id::BOOLEAN:
    case RowsMetadata::Type::Id::COUNTER:
    case RowsMetadata::Type::Id::DECIMAL:
    case RowsMetadata::Type::Id::DOUBLE:
    case RowsMetadata::Type::Id::FLOAT:
    case RowsMetadata::Type::Id::INT:
    case RowsMetadata::Type::Id::TIMESTAMP:
    case RowsMetadata::Type::Id::UUID:
    case RowsMetadata::Type::Id::VARCHAR:
    case RowsMetadata::Type::Id::VARINT:
    case RowsMetadata::Type::Id::TIMEUUID:
    case RowsMetadata::Type::Id::INET:
    case RowsMetadata::Type::Id::DATE:
    case RowsMetadata::Type::Id::TIME:
    case RowsMetadata::Type::Id::SMALLINT:
    case RowsMetadata::Type::Id::TINYINT:
      return;
    case RowsMetadata::Type::Id::LIST:
    case RowsMetadata::Type::Id::SET:
      SerializeType(type->element_type.get(), mesg);
      return;
    case RowsMetadata::Type::Id::MAP:
      SerializeType(type->map_type->key_type.get(), mesg);
      SerializeType(type->map_type->value_type.get(), mesg);
      return;
    case RowsMetadata::Type::Id::UDT:
      SerializeString(type->udt_type->keyspace, mesg);
      SerializeString(type->udt_type->name, mesg);
      SerializeShort(type->udt_type->fields.size(), mesg);
      for (const auto& field : type->udt_type->fields) {
        SerializeString(field.name, mesg);
        SerializeType(field.type.get(), mesg);
      }
      return;
    case RowsMetadata::Type::Id::TUPLE:
      SerializeShort(type->tuple_component_types->size(), mesg);
      for (const auto& component_type : *type->tuple_component_types) {
        SerializeType(component_type.get(), mesg);
      }
      return;

    // default: fall through
  }

  LOG(ERROR) << "Internal error: unknown type id " << static_cast<uint32_t>(type->id);
}

void ResultResponse::SerializeColSpecs(
      const bool has_global_table_spec, const RowsMetadata::GlobalTableSpec& global_table_spec,
      const vector<RowsMetadata::ColSpec>& col_specs, faststring* mesg) {
  if (has_global_table_spec) {
    SerializeString(global_table_spec.keyspace, mesg);
    SerializeString(global_table_spec.table, mesg);
  }
  for (const auto& col_spec : col_specs) {
    if (!has_global_table_spec) {
      SerializeString(col_spec.keyspace, mesg);
      SerializeString(col_spec.table, mesg);
    }
    SerializeString(col_spec.column, mesg);
    SerializeType(&col_spec.type, mesg);
  }
}

void ResultResponse::SerializeRowsMetadata(const RowsMetadata& metadata, faststring* mesg) {
  SerializeInt(metadata.flags, mesg);
  SerializeInt(metadata.col_specs.size(), mesg);
  if (metadata.flags & RowsMetadata::kHasMorePages) {
    SerializeBytes(metadata.paging_state, mesg);
  }
  if (metadata.flags & RowsMetadata::kNoMetadata) {
    return;
  }
  SerializeColSpecs(
      metadata.flags & RowsMetadata::kHasGlobalTableSpec, metadata.global_table_spec,
      metadata.col_specs, mesg);
}

//----------------------------------------------------------------------------------------
VoidResultResponse::VoidResultResponse(const CQLRequest* request)
    : ResultResponse(request, Kind::VOID) {
}

VoidResultResponse::~VoidResultResponse() {
}

void VoidResultResponse::SerializeResultBody(faststring* mesg) {
  // Void result response body is empty
}

//----------------------------------------------------------------------------------------
RowsResultResponse::RowsResultResponse(
    const CQLRequest* request, const RowsMetadata& metadata, const Rows& rows)
    : ResultResponse(request, Kind::ROWS), metadata_(metadata), rows_(rows) {
}

RowsResultResponse::~RowsResultResponse() {
}

void RowsResultResponse::SerializeResultBody(faststring* mesg) {
  SerializeRowsMetadata(metadata_, mesg);
  SerializeInt(rows_.size(), mesg);
  for (const auto& row : rows_) {
    for (const auto& col_value : row) {
      SerializeValue(col_value, mesg);
    }
  }
}

//----------------------------------------------------------------------------------------
PreparedResultResponse::PreparedResultResponse(
    const CQLRequest* request, const PreparedMetadata& prepared_metadata,
    const RowsMetadata& rows_metadata)
    : ResultResponse(request, Kind::PREPARED), prepared_metadata_(prepared_metadata),
      rows_metadata_(rows_metadata) {
}

PreparedResultResponse::~PreparedResultResponse() {
}

void PreparedResultResponse::SerializePreparedMetadata(
    const PreparedMetadata& metadata, faststring* mesg) {
  SerializeInt(metadata.flags, mesg);
  SerializeInt(metadata.col_specs.size(), mesg);
  SerializeInt(metadata.pk_indices.size(), mesg);
  for (const auto& pk_index : metadata.pk_indices) {
    SerializeShort(pk_index, mesg);
  }
  SerializeColSpecs(
      metadata.flags & PreparedMetadata::kHasGlobalTableSpec, metadata.global_table_spec,
      metadata.col_specs, mesg);
}

void PreparedResultResponse::SerializeResultBody(faststring* mesg) {
  SerializeShortBytes(query_id_, mesg);
  SerializePreparedMetadata(prepared_metadata_, mesg);
  SerializeRowsMetadata(rows_metadata_, mesg);
}

//----------------------------------------------------------------------------------------
SetKeyspaceResultResponse::SetKeyspaceResultResponse(
    const CQLRequest* request, const string& keyspace)
    : ResultResponse(request, Kind::SET_KEYSPACE), keyspace_(keyspace) {
}

SetKeyspaceResultResponse::~SetKeyspaceResultResponse() {
}

void SetKeyspaceResultResponse::SerializeResultBody(faststring* mesg) {
  SerializeString(keyspace_, mesg);
}

//----------------------------------------------------------------------------------------
const vector<string> SchemaChangeResultResponse::kEmptyArgumentTypes = {};

SchemaChangeResultResponse::SchemaChangeResultResponse(
    const CQLRequest* request, const string& change_type, const string& target,
    const string& keyspace, const string& object, const vector<string>& argument_types)
    : ResultResponse(request, Kind::SCHEMA_CHANGE), change_type_(change_type),
      keyspace_(keyspace), object_(object), argument_types_(argument_types) {
}

SchemaChangeResultResponse::~SchemaChangeResultResponse() {
}

void SchemaChangeResultResponse::SerializeResultBody(faststring* mesg) {
  SerializeString(change_type_, mesg);
  SerializeString(target_, mesg);
  if (target_ == "KEYSPACE") {
    SerializeString(keyspace_, mesg);
  } else if (target_ == "TABLE" || target_ == "TYPE") {
    SerializeString(keyspace_, mesg);
    SerializeString(object_, mesg);
  } else if (target_ == "FUNCTION" || target_ == "AGGREGATE") {
    SerializeString(keyspace_, mesg);
    SerializeString(object_, mesg);
    SerializeStringList(argument_types_, mesg);
  }
}

//----------------------------------------------------------------------------------------
EventResponse::EventResponse(const CQLRequest* request, const string& event_type)
    : CQLResponse(request, Opcode::EVENT), event_type_(event_type) {
}

EventResponse::~EventResponse() {
}

void EventResponse::SerializeBody(faststring* mesg) {
  SerializeString(event_type_, mesg);
  SerializeEventBody(mesg);
}

//----------------------------------------------------------------------------------------
TopologyChangeEventResponse::TopologyChangeEventResponse(
    const CQLRequest* request, const string& topology_change_type, const Sockaddr& node)
    : EventResponse(request, "TOPOLOGY_CHANGE"), topology_change_type_(topology_change_type),
      node_(node) {
}

TopologyChangeEventResponse::~TopologyChangeEventResponse() {
}

void TopologyChangeEventResponse::SerializeEventBody(faststring* mesg) {
  SerializeString(topology_change_type_, mesg);
  SerializeInet(node_, mesg);
}

//----------------------------------------------------------------------------------------
StatusChangeEventResponse::StatusChangeEventResponse(
    const CQLRequest* request, const string& status_change_type, const Sockaddr& node)
    : EventResponse(request, "STATUS_CHANGE"), status_change_type_(status_change_type),
      node_(node) {
}

StatusChangeEventResponse::~StatusChangeEventResponse() {
}

void StatusChangeEventResponse::SerializeEventBody(faststring* mesg) {
  SerializeString(status_change_type_, mesg);
  SerializeInet(node_, mesg);
}

//----------------------------------------------------------------------------------------
const vector<string> SchemaChangeEventResponse::kEmptyArgumentTypes = {};

SchemaChangeEventResponse::SchemaChangeEventResponse(
    const CQLRequest* request, const string& schema_change_type, const string& target,
    const string& keyspace, const string& object, const vector<string>& argument_types)
    : EventResponse(request, "SCHEMA_CHANGE"), schema_change_type_(schema_change_type),
      target_(target),  keyspace_(keyspace), object_(object), argument_types_(argument_types) {
}

SchemaChangeEventResponse::~SchemaChangeEventResponse() {
}

void SchemaChangeEventResponse::SerializeEventBody(faststring* mesg) {
  SerializeString(schema_change_type_, mesg);
  SerializeString(target_, mesg);
  if (target_ == "KEYSPACE") {
    SerializeString(keyspace_, mesg);
  } else if (target_ == "TABLE" || target_ == "TYPE") {
    SerializeString(keyspace_, mesg);
    SerializeString(object_, mesg);
  } else if (target_ == "FUNCTION" || target_ == "AGGREGATE") {
    SerializeString(keyspace_, mesg);
    SerializeString(object_, mesg);
    SerializeStringList(argument_types_, mesg);
  }
}

//----------------------------------------------------------------------------------------
AuthChallengeResponse::AuthChallengeResponse(const CQLRequest* request, const string& token)
    : CQLResponse(request, Opcode::AUTH_CHALLENGE), token_(token) {
}

AuthChallengeResponse::~AuthChallengeResponse() {
}

void AuthChallengeResponse::SerializeBody(faststring* mesg) {
  SerializeBytes(token_, mesg);
}

//----------------------------------------------------------------------------------------
AuthSuccessResponse::AuthSuccessResponse(const CQLRequest* request, const string& token)
    : CQLResponse(request, Opcode::AUTH_SUCCESS), token_(token) {
}

AuthSuccessResponse::~AuthSuccessResponse() {
}

void AuthSuccessResponse::SerializeBody(faststring* mesg) {
  SerializeBytes(token_, mesg);
}

} // namespace cqlserver
} // namespace yb
