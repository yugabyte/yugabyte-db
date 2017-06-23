// Copyright (c) YugaByte, Inc.

#include <regex>

#include "yb/client/client.h"
#include "yb/common/yql_protocol.pb.h"
#include "yb/cqlserver/cql_message.h"
#include "yb/cqlserver/cql_processor.h"

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
using util::to_char_ptr;

#define RETURN_NOT_ENOUGH(sz)                               \
  do {                                                      \
    if (body_.size() < (sz)) {                              \
      return STATUS(NetworkError, "Truncated CQL message"); \
    }                                                       \
  } while (0)

Status CQLMessage::QueryParameters::GetBindVariable(const std::string& name,
                                                    const int64_t pos,
                                                    const shared_ptr<YQLType>& type,
                                                    YQLValue* value) const {
  const Value* v = nullptr;
  if (!value_map.empty()) {
    const auto itr = value_map.find(name);
    if (itr == value_map.end()) {
      return STATUS_SUBSTITUTE(RuntimeError, "Bind variable \"$0\" not found", name);
    }
    v = &values.at(itr->second);
  } else {
    if (pos < 0 || pos >= values.size()) {
      // Return error with 1-based position.
      return STATUS_SUBSTITUTE(RuntimeError, "Bind variable at position $0 not found", pos + 1);
    }
    v = &values.at(pos);
  }
  switch (v->kind) {
    case Value::Kind::NOT_NULL: {
      if (v->value.empty()) {
        switch (type->main()) {
          case DataType::STRING:
            value->set_string_value("");
            return Status::OK();
          case DataType::BINARY:
            value->set_binary_value("");
            return Status::OK();
          case DataType::NULL_VALUE_TYPE: FALLTHROUGH_INTENDED;
          case DataType::INT8: FALLTHROUGH_INTENDED;
          case DataType::INT16: FALLTHROUGH_INTENDED;
          case DataType::INT32: FALLTHROUGH_INTENDED;
          case DataType::INT64: FALLTHROUGH_INTENDED;
          case DataType::BOOL: FALLTHROUGH_INTENDED;
          case DataType::FLOAT: FALLTHROUGH_INTENDED;
          case DataType::DOUBLE: FALLTHROUGH_INTENDED;
          case DataType::TIMESTAMP: FALLTHROUGH_INTENDED;
          case DataType::DECIMAL: FALLTHROUGH_INTENDED;
          case DataType::VARINT: FALLTHROUGH_INTENDED;
          case DataType::INET: FALLTHROUGH_INTENDED;
          case DataType::LIST: FALLTHROUGH_INTENDED;
          case DataType::MAP: FALLTHROUGH_INTENDED;
          case DataType::SET: FALLTHROUGH_INTENDED;
          case DataType::UUID: FALLTHROUGH_INTENDED;
          case DataType::TIMEUUID:
            value->SetNull();
            return Status::OK();
          case DataType::UNKNOWN_DATA: FALLTHROUGH_INTENDED;
          case DataType::TUPLE: FALLTHROUGH_INTENDED;
          case DataType::TYPEARGS: FALLTHROUGH_INTENDED;
          case DataType::UINT8: FALLTHROUGH_INTENDED;
          case DataType::UINT16: FALLTHROUGH_INTENDED;
          case DataType::UINT32: FALLTHROUGH_INTENDED;
          case DataType::UINT64:
            break;
        }
        return STATUS_SUBSTITUTE(
            RuntimeError, "Unsupported datatype $0", static_cast<int>(type->main()));
      }
      Slice data(v->value);
      return value->Deserialize(type, YQL_CLIENT_CQL, &data);
    }
    case Value::Kind::IS_NULL:
      value->SetNull();
      return Status::OK();
    case Value::Kind::NOT_SET:
      break;
  }
  return STATUS_SUBSTITUTE(
      RuntimeError, "Invalid bind variable kind $0", static_cast<int>(v->kind));
}

namespace {

template<class Type>
Type LoadByte(const Slice& slice, size_t offset) {
  return static_cast<Type>(Load8(slice.data() + offset));
}

template<class Type>
Type LoadShort(const Slice& slice, size_t offset) {
  return static_cast<Type>(NetworkByteOrder::Load16(slice.data() + offset));
}

template<class Type>
Type LoadInt(const Slice& slice, size_t offset) {
  return static_cast<Type>(NetworkByteOrder::Load32(slice.data() + offset));
}

} // namespace

// ------------------------------------ CQL request -----------------------------------
bool CQLRequest::ParseRequest(
  const Slice& mesg, unique_ptr<CQLRequest>* request, unique_ptr<CQLResponse>* error_response) {

  *request = nullptr;
  *error_response = nullptr;

  if (mesg.size() < kMessageHeaderLength) {
    error_response->reset(
        new ErrorResponse(
            static_cast<StreamId>(0), ErrorResponse::Code::PROTOCOL_ERROR, "Incomplete header"));
    return false;
  }

  Header header(
      LoadByte<Version>(mesg, kHeaderPosVersion),
      LoadByte<Flags>(mesg, kHeaderPosFlags),
      ParseStreamId(mesg),
      LoadByte<Opcode>(mesg, kHeaderPosOpcode));

  uint32_t length = LoadInt<uint32_t>(mesg, kHeaderPosLength);
  DVLOG(4) << "CQL message "
           << "version 0x" << std::hex << static_cast<uint32_t>(header.version)   << " "
           << "flags 0x"   << std::hex << static_cast<uint32_t>(header.flags)     << " "
           << "stream id " << std::dec << static_cast<uint32_t>(header.stream_id) << " "
           << "opcode 0x"  << std::hex << static_cast<uint32_t>(header.opcode)    << " "
           << "length "    << std::dec << static_cast<uint32_t>(length);

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
            *request->get(), ErrorResponse::Code::PROTOCOL_ERROR, status.message().ToString()));
  } else if (!request->get()->body_.empty()) {
    // Flag error when there are bytes remaining after we have parsed the whole request body
    // according to the protocol. Either the request's length field from the client is
    // wrong. Or we could have a bug in our parser.
    error_response->reset(
        new ErrorResponse(
            *request->get(), ErrorResponse::Code::PROTOCOL_ERROR, "Request length too long"));
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

Status CQLRequest::ParseUUID(string* value) {
  RETURN_NOT_ENOUGH(kUUIDSize);
  *value = string(to_char_ptr(body_.data()), kUUIDSize);
  body_.remove_prefix(kUUIDSize);
  DVLOG(4) << "CQL uuid " << *value;
  return Status::OK();
}

Status CQLRequest::ParseTimeUUID(string* value) {
  RETURN_NOT_ENOUGH(kUUIDSize);
  *value = string(to_char_ptr(body_.data()), kUUIDSize);
  body_.remove_prefix(kUUIDSize);
  DVLOG(4) << "CQL timeuuid " << *value;
  return Status::OK();
}

Status CQLRequest::ParseStringList(vector<string>* list) {
  DVLOG(4) << "CQL string list ...";
  uint16_t count = 0;
  RETURN_NOT_OK(ParseShort(&count));
  list->resize(count);
  for (uint16_t i = 0; i < count; ++i) {
    RETURN_NOT_OK(ParseString(&list->at(i)));
  }
  return Status::OK();
}

Status CQLRequest::ParseInet(Endpoint* value) {
  std::string ipaddr;
  int32_t port = 0;
  RETURN_NOT_OK(ParseBytes("CQL ipaddr", &CQLRequest::ParseByte, &ipaddr));
  RETURN_NOT_OK(ParseInt(&port));
  if (port < 0 || port > 65535) {
    return STATUS(NetworkError, "Invalid inet port");
  }
  IpAddress address;
  if (ipaddr.size() == boost::asio::ip::address_v4::bytes_type().size()) {
    boost::asio::ip::address_v4::bytes_type bytes;
    memcpy(bytes.data(), ipaddr.data(), ipaddr.size());
    address = boost::asio::ip::address_v4(bytes);
  } else if (ipaddr.size() == boost::asio::ip::address_v6::bytes_type().size()) {
    boost::asio::ip::address_v6::bytes_type bytes;
    memcpy(bytes.data(), ipaddr.data(), ipaddr.size());
    address = boost::asio::ip::address_v6(bytes);
  } else {
    return STATUS_SUBSTITUTE(NetworkError, "Invalid size of ipaddr: $0", ipaddr.size());
  }
  *value = Endpoint(address, port);
  DVLOG(4) << "CQL inet " << *value;
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
  // Save data pointer to assign the value with length bytes below.
  const uint8_t* data = body_.data();
  int32_t length = 0;
  RETURN_NOT_OK(ParseInt(&length));
  if (length >= 0) {
    value->kind = Value::Kind::NOT_NULL;
    if (length > 0) {
      RETURN_NOT_ENOUGH(length);
      value->value.assign(to_char_ptr(data), kIntSize + length);
      body_.remove_prefix(length);
      DVLOG(4) << "CQL value bytes " << value->value;
    }
  } else if (VersionIsCompatible(kV4Version)) {
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
    params->values.resize(count);
    for (uint16_t i = 0; i < count; ++i) {
      Value& value = params->values[i];
      RETURN_NOT_OK(ParseValue(with_name, &value));
      if (with_name) {
        params->value_map[value.name] = i;
      }
    }
  }
  if (params->flags & CQLMessage::QueryParameters::kWithPageSizeFlag) {
    int32_t page_size = 0;
    RETURN_NOT_OK(ParseInt(&page_size));
    params->set_page_size(page_size);
  }
  if (params->flags & CQLMessage::QueryParameters::kWithPagingStateFlag) {
    string paging_state;
    RETURN_NOT_OK(ParseBytes(&paging_state));
    RETURN_NOT_OK(params->set_paging_state(paging_state));
  }
  if (params->flags & CQLMessage::QueryParameters::kWithSerialConsistencyFlag) {
    RETURN_NOT_OK(ParseConsistency(&params->serial_consistency));
  }
  if (params->flags & CQLMessage::QueryParameters::kWithDefaultTimestampFlag) {
    RETURN_NOT_OK(ParseLong(&params->default_timestamp));
  }
  return Status::OK();
}

void CQLRequest::ExecuteAsync(CQLProcessor* processor, Callback<void(CQLResponse*)> cb) {
  cb.Run(Execute(processor));
}

// ------------------------------ Individual CQL requests -----------------------------------
StartupRequest::StartupRequest(const Header& header, const Slice& body)
    : CQLRequest(header, body) {
}

StartupRequest::~StartupRequest() {
}

Status StartupRequest::ParseBody() {
  return ParseStringMap(&options_);
}

CQLResponse* StartupRequest::Execute(CQLProcessor *processor) {
  for (const auto& option : options_) {
    const auto& name = option.first;
    const auto& value = option.second;
    const auto it = SupportedResponse::options_.find(name);
    if (it == SupportedResponse::options_.end() ||
        std::find(it->second.begin(), it->second.end(), value) == it->second.end()) {
      return new ErrorResponse(
          *this, ErrorResponse::Code::PROTOCOL_ERROR, "Unsupported option " + name);
    }
  }
  return new ReadyResponse(*this);
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

CQLResponse* AuthResponseRequest::Execute(CQLProcessor *processor) {
  // TODO(Robert): authentication support
  return new ErrorResponse(*this, ErrorResponse::Code::PROTOCOL_ERROR, "Not implemented yet");
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

CQLResponse* OptionsRequest::Execute(CQLProcessor *processor) {
  return new SupportedResponse(*this);
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

CQLResponse* QueryRequest::Execute(CQLProcessor *processor) {
  LOG(FATAL) << "Execute should only be used asynchrounsly on QueryRequest";
  return nullptr;
}

void QueryRequest::ExecuteAsync(CQLProcessor* processor, Callback<void(CQLResponse*)> cb) {
  processor->ProcessQuery(*this, cb);
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

CQLResponse* PrepareRequest::Execute(CQLProcessor *processor) {
  return processor->ProcessPrepare(*this);
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

CQLResponse* ExecuteRequest::Execute(CQLProcessor *processor) {
  LOG(FATAL) << "Execute should only be used asynchrounsly on ExecuteRequest";
  return nullptr;
}

void ExecuteRequest::ExecuteAsync(CQLProcessor* processor, Callback<void(CQLResponse*)> cb) {
  processor->ProcessExecute(*this, cb);
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
  queries_.resize(query_count);
  for (uint16_t i = 0; i < query_count; ++i) {
    Query& query = queries_[i];
    uint8_t is_prepared_query = 0;
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
    query.params.values.resize(value_count);
    for (uint16_t j = 0; j < value_count; ++j) {
      // with_name is not possible in the protocol due to a design flaw. See JIRA CASSANDRA-10246.
      RETURN_NOT_OK(ParseValue(false /* with_name */, &query.params.values[j]));
    }
  }

  Consistency consistency = Consistency::ANY;
  QueryParameters::Flags flags = 0;
  Consistency serial_consistency = Consistency::ANY;
  int64_t default_timestamp = 0;
  RETURN_NOT_OK(ParseConsistency(&consistency));
  RETURN_NOT_OK(ParseByte(&flags));
  if (flags & CQLMessage::QueryParameters::kWithSerialConsistencyFlag) {
    RETURN_NOT_OK(ParseConsistency(&serial_consistency));
  }
  if (flags & CQLMessage::QueryParameters::kWithDefaultTimestampFlag) {
    RETURN_NOT_OK(ParseLong(&default_timestamp));
  }

  for (Query& query : queries_) {
    QueryParameters& params = query.params;
    params.consistency = consistency;
    params.flags = flags;
    params.serial_consistency = serial_consistency;
    params.default_timestamp = default_timestamp;
  }
  return Status::OK();
}

CQLResponse* BatchRequest::Execute(CQLProcessor *processor) {
  LOG(FATAL) << "Execute should only be used asynchrounsly on BatchRequest";
  return nullptr;
}

void BatchRequest::ExecuteAsync(CQLProcessor* processor, Callback<void(CQLResponse*)> cb) {
  processor->ProcessBatch(*this, cb);
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

CQLResponse* RegisterRequest::Execute(CQLProcessor *processor) {
  // TODO(Robert): implement real event responses
  return new ReadyResponse(*this);
}

// ------------------------------------ CQL response -----------------------------------
CQLResponse::CQLResponse(const CQLRequest& request, const Opcode opcode)
    : CQLMessage(
          Header(
              request.version() | kResponseVersion, request.flags(), request.stream_id(),
              opcode)) {
}

CQLResponse::CQLResponse(const StreamId stream_id, const Opcode opcode)
    : CQLMessage(Header(kCurrentVersion | kResponseVersion, 0, stream_id, opcode)) {
}

CQLResponse::~CQLResponse() {
}

// Short-hand macros for serializing fields from the message header
#define SERIALIZE_BYTE(buf, pos, value) \
  Store8(&(buf)[pos], static_cast<uint8_t>(value))
#define SERIALIZE_SHORT(buf, pos, value) \
  NetworkByteOrder::Store16(&(buf)[pos], static_cast<uint16_t>(value))
#define SERIALIZE_INT(buf, pos, value) \
  NetworkByteOrder::Store32(&(buf)[pos], static_cast<int32_t>(value))
#define SERIALIZE_LONG(buf, pos, value) \
  NetworkByteOrder::Store64(&(buf)[pos], static_cast<int64_t>(value))

void CQLResponse::Serialize(faststring* mesg) const {
  const size_t start_pos = mesg->size(); // save the start position
  SerializeHeader(mesg);
  SerializeBody(mesg);
  SERIALIZE_INT(
      mesg->data(), start_pos + kHeaderPosLength, mesg->size() - start_pos - kMessageHeaderLength);
}

void CQLResponse::SerializeHeader(faststring* mesg) const {
  uint8_t buffer[kMessageHeaderLength];
  SERIALIZE_BYTE(buffer, kHeaderPosVersion, version());
  SERIALIZE_BYTE(buffer, kHeaderPosFlags, flags());
  SERIALIZE_SHORT(buffer, kHeaderPosStreamId, stream_id());
  SERIALIZE_INT(buffer, kHeaderPosLength, 0);
  SERIALIZE_BYTE(buffer, kHeaderPosOpcode, opcode());
  mesg->append(buffer, sizeof(buffer));
}

#undef SERIALIZE_BYTE
#undef SERIALIZE_SHORT
#undef SERIALIZE_INT
#undef SERIALIZE_LONG

// --------------------------- Serialization utility functions -------------------------------
namespace {

using yb::cqlserver::CQLMessage;

// Serialize a CQL number (8, 16, 32 and 64-bit integer). <num_type> is the integer type.
// <converter> converts the number from machine byte-order to network order and <data_type>
// is the coverter's return type. The converter's return type <data_type> is unsigned while
// <num_type> may be signed or unsigned.
template<typename num_type, typename data_type>
void SerializeNum(void (*converter)(void *, data_type), const num_type val, faststring* mesg) {
  data_type byte_value;
  (*converter)(&byte_value, static_cast<data_type>(val));
  mesg->append(&byte_value, sizeof(byte_value));
}

// Serialize a CQL byte stream (string or bytes). <len_type> is the length type.
// <len_serializer> serializes the byte length from machine byte-order to network order.
template<typename len_type>
inline void SerializeBytes(
    void (*len_serializer)(len_type, faststring* mesg), string val,
    faststring* mesg) {
  (*len_serializer)(static_cast<len_type>(val.size()), mesg);
  mesg->append(val);
}

inline void SerializeByte(const uint8_t value, faststring* mesg) {
  static_assert(sizeof(value) == CQLMessage::kByteSize, "inconsistent byte size");
  SerializeNum(Store8, value, mesg);
}

inline void SerializeShort(const uint16_t value, faststring* mesg) {
  static_assert(sizeof(value) == CQLMessage::kShortSize, "inconsistent short size");
  SerializeNum(NetworkByteOrder::Store16, value, mesg);
}

inline void SerializeInt(const int32_t value, faststring* mesg) {
  static_assert(sizeof(value) == CQLMessage::kIntSize, "inconsistent int size");
  SerializeNum(NetworkByteOrder::Store32, value, mesg);
}

#if 0 // Save this function for future use
inline void SerializeLong(const int64_t value, faststring* mesg) {
  static_assert(sizeof(value) == CQLMessage::kLongSize, "inconsistent long size");
  SerializeNum(NetworkByteOrder::Store64, value, mesg);
}
#endif

inline void SerializeString(const string& value, faststring* mesg) {
  SerializeBytes(&SerializeShort, value, mesg);
}

#if 0 // Save these functions for future use
inline void SerializeLongString(const string& value, faststring* mesg) {
  SerializeBytes(&SerializeInt, value, mesg);
}
#endif

inline void SerializeShortBytes(const string& value, faststring* mesg) {
  SerializeBytes(&SerializeShort, value, mesg);
}

inline void SerializeBytes(const string& value, faststring* mesg) {
  SerializeBytes(&SerializeInt, value, mesg);
}

#if 0 // Save these functions for future use
inline void SerializeConsistency(const CQLMessage::Consistency consistency, faststring* mesg) {
  static_assert(
      sizeof(consistency) == CQLMessage::kConsistencySize,
      "inconsistent consistency size");
  SerializeNum(NetworkByteOrder::Store16, consistency, mesg);
}

void SerializeUUID(const string& value, faststring* mesg) {
  if (value.size() == CQLMessage::kUUIDSize) {
    mesg->append(value);
  } else {
    LOG(ERROR) << "Internal error: inconsistent UUID size: " << value.size();
    uint8_t empty_uuid[CQLMessage::kUUIDSize] = {0};
    mesg->append(empty_uuid, sizeof(empty_uuid));
  }
}

void SerializeTimeUUID(const string& value, faststring* mesg) {
  if (value.size() == CQLMessage::kUUIDSize) {
    mesg->append(value);
  } else {
    LOG(ERROR) << "Internal error: inconsistent TimeUUID size: " << value.size();
    uint8_t empty_uuid[CQLMessage::kUUIDSize] = {0};
    mesg->append(empty_uuid, sizeof(empty_uuid));
  }
}
#endif

void SerializeStringList(const vector<string>& list, faststring* mesg) {
  SerializeShort(list.size(), mesg);
  for (int i = 0; i < list.size(); ++i) {
    SerializeString(list[i], mesg);
  }
}

void SerializeInet(const Endpoint& value, faststring* mesg) {
  auto address = value.address();
  if (address.is_v4()) {
    auto bytes = address.to_v4().to_bytes();
    SerializeByte(bytes.size(), mesg);
    mesg->append(bytes.data(), bytes.size());
  } else {
    auto bytes = address.to_v6().to_bytes();
    SerializeByte(bytes.size(), mesg);
    mesg->append(bytes.data(), bytes.size());
  }
  const uint16_t port = value.port();
  SerializeInt(port, mesg);
}

#if 0 // Save these functions for future use
void SerializeStringMap(const unordered_map<string, string>& map, faststring* mesg) {
  SerializeShort(map.size(), mesg);
  for (const auto& element : map) {
    SerializeString(element.first, mesg);
    SerializeString(element.second, mesg);
  }
}
#endif

void SerializeStringMultiMap(const unordered_map<string, vector<string>>& map, faststring* mesg) {
  SerializeShort(map.size(), mesg);
  for (const auto& element : map) {
    SerializeString(element.first, mesg);
    SerializeStringList(element.second, mesg);
  }
}

#if 0 // Save these functions for future use
void SerializeBytesMap(const unordered_map<string, string>& map, faststring* mesg) {
  SerializeShort(map.size(), mesg);
  for (const auto& element : map) {
    SerializeString(element.first, mesg);
    SerializeBytes(element.second, mesg);
  }
}

void SerializeValue(const CQLMessage::Value& value, faststring* mesg) {
  switch (value.kind) {
    case CQLMessage::Value::Kind::NOT_NULL:
      SerializeInt(value.value.size(), mesg);
      mesg->append(value.value);
      return;
    case CQLMessage::Value::Kind::IS_NULL:
      SerializeInt(-1, mesg);
      return;
    case CQLMessage::Value::Kind::NOT_SET: // NOT_SET value kind should appear in request msg only.
      break;
      // default: fall through
  }
  LOG(ERROR) << "Internal error: invalid/unknown value kind " << static_cast<uint32_t>(value.kind);
  SerializeInt(-1, mesg);
}
#endif

} // namespace

// ------------------------------ Individual CQL responses -----------------------------------
ErrorResponse::ErrorResponse(const CQLRequest& request, const Code code, const string& message)
    : CQLResponse(request, Opcode::ERROR), code_(code), message_(message) {
}

ErrorResponse::ErrorResponse(const CQLRequest& request, const Code code, const Status& status)
    : CQLResponse(request, Opcode::ERROR), code_(code),
      message_(to_char_ptr(status.message().data())) {
}

ErrorResponse::ErrorResponse(const StreamId stream_id, const Code code, const string& message)
    : CQLResponse(stream_id, Opcode::ERROR), code_(code), message_(message) {
}

ErrorResponse::~ErrorResponse() {
}

void ErrorResponse::SerializeBody(faststring* mesg) const {
  SerializeInt(static_cast<int32_t>(code_), mesg);
  SerializeString(message_, mesg);
  SerializeErrorBody(mesg);
}

void ErrorResponse::SerializeErrorBody(faststring* mesg) const {
}

//----------------------------------------------------------------------------------------
UnpreparedErrorResponse::UnpreparedErrorResponse(const CQLRequest& request, const QueryId& query_id)
    : ErrorResponse(request, Code::UNPREPARED, "Unprepared query"), query_id_(query_id) {
}

UnpreparedErrorResponse::~UnpreparedErrorResponse() {
}

void UnpreparedErrorResponse::SerializeErrorBody(faststring* mesg) const {
  SerializeShortBytes(query_id_, mesg);
}

//----------------------------------------------------------------------------------------
ReadyResponse::ReadyResponse(const CQLRequest& request) : CQLResponse(request, Opcode::READY) {
}

ReadyResponse::~ReadyResponse() {
}

void ReadyResponse::SerializeBody(faststring* mesg) const {
  // Ready body is empty
}

//----------------------------------------------------------------------------------------
AuthenticateResponse::AuthenticateResponse(const CQLRequest& request, const string& authenticator)
    : CQLResponse(request, Opcode::AUTHENTICATE), authenticator_(authenticator) {
}

AuthenticateResponse::~AuthenticateResponse() {
}

void AuthenticateResponse::SerializeBody(faststring* mesg) const {
  SerializeString(authenticator_, mesg);
}

//----------------------------------------------------------------------------------------
const unordered_map<string, vector<string>> SupportedResponse::options_ = {
  {"COMPRESSION", { } },
  {"CQL_VERSION", {"3.0.0" /* minimum */, "3.4.2" /* current */} }
};

SupportedResponse::SupportedResponse(const CQLRequest& request)
    : CQLResponse(request, Opcode::SUPPORTED) {
}

SupportedResponse::~SupportedResponse() {
}

void SupportedResponse::SerializeBody(faststring* mesg) const {
  SerializeStringMultiMap(options_, mesg);
}

//----------------------------------------------------------------------------------------
ResultResponse::ResultResponse(const CQLRequest& request, const Kind kind)
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

ResultResponse::RowsMetadata::Type::Type(const shared_ptr<YQLType>& type) {
  switch (type->main()) {
    case DataType::INT8:
      id = Id::TINYINT;
      return;
    case DataType::INT16:
      id = Id::SMALLINT;
      return;
    case DataType::INT32:
      id = Id::INT;
      return;
    case DataType::INT64:
      id = Id::BIGINT;
      return;
    case DataType::FLOAT:
      id = Id::FLOAT;
      return;
    case DataType::DOUBLE:
      id = Id::DOUBLE;
      return;
    case DataType::STRING:
      id = Id::VARCHAR;
      return;
    case DataType::BOOL:
      id = Id::BOOLEAN;
      return;
    case DataType::TIMESTAMP:
      id = Id::TIMESTAMP;
      return;
    case DataType::INET:
      id = Id::INET;
      return;
    case DataType::UUID:
      id = Id::UUID;
      return;
    case DataType::TIMEUUID:
      id = Id::TIMEUUID;
      return;
    case DataType::BINARY:
      id = Id::BLOB;
      return;
    case DataType::LIST:
      id = Id::LIST;
      new (&element_type) shared_ptr<const Type>(
          std::make_shared<const Type>(Type(type->params()[0])));
      return;
    case DataType::SET:
      id = Id::SET;
      new (&element_type) shared_ptr<const Type>(
          std::make_shared<const Type>(Type(type->params()[0])));
      return;
    case DataType::MAP: {
      id = Id::MAP;
      auto key = std::make_shared<const Type>(Type(type->params()[0]));
      auto value = std::make_shared<const Type>(Type(type->params()[1]));
      new (&map_type) shared_ptr<const MapType>(std::make_shared<MapType>(MapType{key, value}));
      return;
    }
    case DataType::DECIMAL:
      id = Id::DECIMAL;
      return;

    case DataType::NULL_VALUE_TYPE: FALLTHROUGH_INTENDED;
    case DataType::VARINT: FALLTHROUGH_INTENDED;
    case DataType::TUPLE: FALLTHROUGH_INTENDED;
    case DataType::TYPEARGS: FALLTHROUGH_INTENDED;

    case DataType::UINT8:  FALLTHROUGH_INTENDED;
    case DataType::UINT16: FALLTHROUGH_INTENDED;
    case DataType::UINT32: FALLTHROUGH_INTENDED;
    case DataType::UINT64: FALLTHROUGH_INTENDED;
    case DataType::UNKNOWN_DATA:
      break;

    // default: fall through
  }

  LOG(ERROR) << "Internal error: invalid/unsupported type " << type->ToString();
}

ResultResponse::RowsMetadata::Type::~Type() {
  switch (id) {
    case Id::CUSTOM:
      custom_class_name.~basic_string();
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

ResultResponse::RowsMetadata::RowsMetadata()
    : flags(kNoMetadata),
      paging_state(""),
      global_table_spec("" /* keyspace */, "" /* table_name */),
      col_count(0) {
}

ResultResponse::RowsMetadata::RowsMetadata(const client::YBTableName& table_name,
                                           const vector<ColumnSchema>& columns,
                                           const string& paging_state,
                                           bool no_metadata)
    : flags((no_metadata ? kNoMetadata : kHasGlobalTableSpec) |
            (!paging_state.empty() ? kHasMorePages : 0)),
      paging_state(paging_state),
      global_table_spec(no_metadata ? "" : table_name.resolved_namespace_name(),
                        no_metadata ? "" : table_name.table_name()),
      col_count(columns.size()) {
  if (!no_metadata) {
    col_specs.reserve(col_count);
    for (const auto column : columns) {
      col_specs.emplace_back(column.name(), Type(column.type()));
    }
  }
}

void ResultResponse::SerializeBody(faststring* mesg) const {
  SerializeInt(static_cast<int32_t>(kind_), mesg);
  SerializeResultBody(mesg);
}

void ResultResponse::SerializeType(const RowsMetadata::Type* type, faststring* mesg) const {
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
      const vector<RowsMetadata::ColSpec>& col_specs, faststring* mesg) const {
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

void ResultResponse::SerializeRowsMetadata(const RowsMetadata& metadata, faststring* mesg) const {
  SerializeInt(metadata.flags, mesg);
  SerializeInt(metadata.col_count, mesg);
  if (metadata.flags & RowsMetadata::kHasMorePages) {
    SerializeBytes(metadata.paging_state, mesg);
  }
  if (metadata.flags & RowsMetadata::kNoMetadata) {
    return;
  }
  CHECK_EQ(metadata.col_count, metadata.col_specs.size());
  SerializeColSpecs(
      metadata.flags & RowsMetadata::kHasGlobalTableSpec, metadata.global_table_spec,
      metadata.col_specs, mesg);
}

//----------------------------------------------------------------------------------------
VoidResultResponse::VoidResultResponse(const CQLRequest& request)
    : ResultResponse(request, Kind::VOID) {
}

VoidResultResponse::~VoidResultResponse() {
}

void VoidResultResponse::SerializeResultBody(faststring* mesg) const {
  // Void result response body is empty
}

//----------------------------------------------------------------------------------------
RowsResultResponse::RowsResultResponse(
    const QueryRequest& request, const sql::RowsResult::SharedPtr& result)
    : ResultResponse(request, Kind::ROWS), result_(result),
      skip_metadata_(request.params_.flags & CQLMessage::QueryParameters::kSkipMetadataFlag) {
}

RowsResultResponse::RowsResultResponse(
    const ExecuteRequest& request, const sql::RowsResult::SharedPtr& result)
    : ResultResponse(request, Kind::ROWS), result_(result),
      skip_metadata_(request.params_.flags & CQLMessage::QueryParameters::kSkipMetadataFlag) {
}

RowsResultResponse::~RowsResultResponse() {
}

void RowsResultResponse::SerializeResultBody(faststring* mesg) const {
  SerializeRowsMetadata(
      RowsMetadata(result_->table_name(), result_->column_schemas(),
                   result_->paging_state(), skip_metadata_), mesg);
  mesg->append(result_->rows_data());
}

//----------------------------------------------------------------------------------------
PreparedResultResponse::PreparedMetadata::PreparedMetadata() {
}

PreparedResultResponse::PreparedMetadata::PreparedMetadata(
    const client::YBTableName& table_name, const vector<ColumnSchema>& bind_variable_schemas)
    : flags(kHasGlobalTableSpec),
      global_table_spec(table_name.resolved_namespace_name(), table_name.table_name()) {
  // TODO(robert): populate primary-key indices.
  col_specs.reserve(bind_variable_schemas.size());
  for (const auto var : bind_variable_schemas) {
    col_specs.emplace_back(var.name(), RowsMetadata::Type(var.type()));
  }
}

PreparedResultResponse::PreparedResultResponse(const CQLRequest& request, const QueryId& query_id)
    : ResultResponse(request, Kind::PREPARED), query_id_(query_id) {
}

PreparedResultResponse::PreparedResultResponse(
    const CQLRequest& request, const QueryId& query_id, const sql::PreparedResult& result)
    : ResultResponse(request, Kind::PREPARED), query_id_(query_id),
      prepared_metadata_(result.table_name(), result.bind_variable_schemas()),
      rows_metadata_(!result.column_schemas().empty() ?
                     RowsMetadata(
                         result.table_name(), result.column_schemas(),
                         "" /* paging_state */, false /* no_metadata */) :
                     RowsMetadata()) {
}

PreparedResultResponse::~PreparedResultResponse() {
}

void PreparedResultResponse::SerializePreparedMetadata(
    const PreparedMetadata& metadata, faststring* mesg) const {
  SerializeInt(metadata.flags, mesg);
  SerializeInt(metadata.col_specs.size(), mesg);
  if (VersionIsCompatible(kV4Version)) {
    SerializeInt(metadata.pk_indices.size(), mesg);
    for (const auto& pk_index : metadata.pk_indices) {
      SerializeShort(pk_index, mesg);
    }
  }
  SerializeColSpecs(
      metadata.flags & PreparedMetadata::kHasGlobalTableSpec, metadata.global_table_spec,
      metadata.col_specs, mesg);
}

void PreparedResultResponse::SerializeResultBody(faststring* mesg) const {
  SerializeShortBytes(query_id_, mesg);
  SerializePreparedMetadata(prepared_metadata_, mesg);
  SerializeRowsMetadata(rows_metadata_, mesg);
}

//----------------------------------------------------------------------------------------
SetKeyspaceResultResponse::SetKeyspaceResultResponse(
    const CQLRequest& request, const sql::SetKeyspaceResult& result)
    : ResultResponse(request, Kind::SET_KEYSPACE), keyspace_(result.keyspace()) {
}

SetKeyspaceResultResponse::~SetKeyspaceResultResponse() {
}

void SetKeyspaceResultResponse::SerializeResultBody(faststring* mesg) const {
  SerializeString(keyspace_, mesg);
}

//----------------------------------------------------------------------------------------
SchemaChangeResultResponse::SchemaChangeResultResponse(
    const CQLRequest& request, const sql::SchemaChangeResult& result)
    : ResultResponse(request, Kind::SCHEMA_CHANGE),
      change_type_(result.change_type()), target_(result.object_type()),
      keyspace_(result.keyspace_name()), object_(result.object_name()) {
}

SchemaChangeResultResponse::~SchemaChangeResultResponse() {
}

void SchemaChangeResultResponse::Serialize(faststring* mesg) const {
  ResultResponse::Serialize(mesg);
  // TODO: Replace this hack that piggybacks a SCHEMA_CHANGE event along a SCHEMA_CHANGE result
  // response with a formal event notification mechanism.
  SchemaChangeEventResponse event(change_type_, target_, keyspace_, object_, argument_types_);
  event.Serialize(mesg);
}

void SchemaChangeResultResponse::SerializeResultBody(faststring* mesg) const {
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
EventResponse::EventResponse(const string& event_type)
    : CQLResponse(kEventStreamId, Opcode::EVENT), event_type_(event_type) {
}

EventResponse::~EventResponse() {
}

void EventResponse::SerializeBody(faststring* mesg) const {
  SerializeString(event_type_, mesg);
  SerializeEventBody(mesg);
}

std::string EventResponse::ToString() const {
  return event_type_ + ":" + BodyToString();
}

//----------------------------------------------------------------------------------------
TopologyChangeEventResponse::TopologyChangeEventResponse(const string& topology_change_type,
                                                         const Endpoint& node)
    : EventResponse("TOPOLOGY_CHANGE"), topology_change_type_(topology_change_type),
      node_(node) {
}

TopologyChangeEventResponse::~TopologyChangeEventResponse() {
}

void TopologyChangeEventResponse::SerializeEventBody(faststring* mesg) const {
  SerializeString(topology_change_type_, mesg);
  SerializeInet(node_, mesg);
}

std::string TopologyChangeEventResponse::BodyToString() const {
  return topology_change_type_;
}

//----------------------------------------------------------------------------------------
StatusChangeEventResponse::StatusChangeEventResponse(const string& status_change_type,
                                                     const Endpoint& node)
    : EventResponse("STATUS_CHANGE"), status_change_type_(status_change_type),
      node_(node) {
}

StatusChangeEventResponse::~StatusChangeEventResponse() {
}

void StatusChangeEventResponse::SerializeEventBody(faststring* mesg) const {
  SerializeString(status_change_type_, mesg);
  SerializeInet(node_, mesg);
}

std::string StatusChangeEventResponse::BodyToString() const {
  return status_change_type_;
}

//----------------------------------------------------------------------------------------
const vector<string> SchemaChangeEventResponse::kEmptyArgumentTypes = {};

SchemaChangeEventResponse::SchemaChangeEventResponse(
    const string& change_type, const string& target,
    const string& keyspace, const string& object, const vector<string>& argument_types)
    : EventResponse("SCHEMA_CHANGE"), change_type_(change_type), target_(target),
      keyspace_(keyspace), object_(object), argument_types_(argument_types) {
}

SchemaChangeEventResponse::~SchemaChangeEventResponse() {
}

std::string SchemaChangeEventResponse::BodyToString() const {
  return change_type_;
}

void SchemaChangeEventResponse::SerializeEventBody(faststring* mesg) const {
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
AuthChallengeResponse::AuthChallengeResponse(const CQLRequest& request, const string& token)
    : CQLResponse(request, Opcode::AUTH_CHALLENGE), token_(token) {
}

AuthChallengeResponse::~AuthChallengeResponse() {
}

void AuthChallengeResponse::SerializeBody(faststring* mesg) const {
  SerializeBytes(token_, mesg);
}

//----------------------------------------------------------------------------------------
AuthSuccessResponse::AuthSuccessResponse(const CQLRequest& request, const string& token)
    : CQLResponse(request, Opcode::AUTH_SUCCESS), token_(token) {
}

AuthSuccessResponse::~AuthSuccessResponse() {
}

void AuthSuccessResponse::SerializeBody(faststring* mesg) const {
  SerializeBytes(token_, mesg);
}

CQLServerEvent::CQLServerEvent(std::unique_ptr<EventResponse> event_response)
    : event_response_(std::move(event_response)) {
  CHECK_NOTNULL(event_response_.get());
  faststring temp;
  event_response_->Serialize(&temp);
  serialized_response_ = util::RefCntBuffer(temp);
}

void CQLServerEvent::Serialize(std::deque<util::RefCntBuffer>* output) const {
  output->push_back(serialized_response_);
}

std::string CQLServerEvent::ToString() const {
  return event_response_->ToString();
}

CQLServerEventList::CQLServerEventList() {
}

void CQLServerEventList::Transferred(const Status& status) {
  if (!status.ok()) {
    LOG(WARNING) << "Transfer of CQL server event failed: " << status.ToString();
  }
}

void CQLServerEventList::Serialize(std::deque<util::RefCntBuffer>* output) const {
  for (const auto& cql_server_event : cql_server_events_) {
    cql_server_event->Serialize(output);
  }
}

std::string CQLServerEventList::ToString() const {
  std::string ret = "";
  for (const auto& cql_server_event : cql_server_events_) {
    if (!ret.empty()) {
      ret += ", ";
    }
    ret += cql_server_event->ToString();
  }
  return ret;
}

void CQLServerEventList::AddEvent(std::unique_ptr<CQLServerEvent> event) {
  cql_server_events_.push_back(std::move(event));
}

}  // namespace cqlserver
}  // namespace yb
