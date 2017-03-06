// Copyright (c) YugaByte, Inc.
//
// This file contains the CQLRequest and CQLResponse classes that implement the CQL protocol for
// the CQL server. For the protocol spec, see https://github.com/apache/cassandra/tree/trunk/doc
//   - native_protocol_v3.spec
//   - native_protocol_v4.spec
//   - native_protocol_v5.spec

#ifndef YB_CQLSERVER_CQL_MESSAGE_H
#define YB_CQLSERVER_CQL_MESSAGE_H

#include <stdint.h>
#include <memory>
#include <set>
#include <unordered_map>

#include "yb/common/wire_protocol.h"
#include "yb/sql/statement.h"
#include "yb/sql/util/rows_result.h"
#include "yb/util/slice.h"
#include "yb/util/status.h"
#include "yb/util/net/sockaddr.h"
#include "yb/gutil/strings/split.h"
#include "yb/util/pb_util.h"

namespace yb {
namespace cqlserver {

// Forward declaration for owner. The CQLProcessor owns the message that it's processing.
class CQLProcessor;

class CQLRequest;
class CQLResponse;

// ---------------------------------- Generic CQL message ---------------------------------
class CQLMessage {

 public:
  //
  // Each CQL message consists of a 9-byte header:
  //   0         8        16        24        32         40
  //   +---------+---------+---------+---------+---------+
  //   | version |  flags  |      stream       | opcode  |
  //   +---------+---------+---------+---------+---------+
  //   |                length                 |
  //   +---------+---------+---------+---------+
  //   |            ...  body ...              |
  //   +----------------------------------------
  static constexpr int32_t kHeaderPosVersion    = 0;
  static constexpr int32_t kHeaderPosFlags      = 1;
  static constexpr int32_t kHeaderPosStreamId   = 2;
  static constexpr int32_t kHeaderPosOpcode     = 4;
  static constexpr int32_t kHeaderPosLength     = 5;
  static constexpr int32_t kMessageHeaderLength = 9;

  static constexpr int32_t kMaxMessageLength    = 256 * 1024 * 1024; // Max msg length per CQL spec

  // Datatypes for CQL message header
  using Version = uint8_t;
  static constexpr Version kV3Version       = 0x03;
  static constexpr Version kV4Version       = 0x04; // Current Cassandra production version
  static constexpr Version kMinimumVersion  = kV3Version;
  static constexpr Version kCurrentVersion  = kV4Version;
  static constexpr Version kV5Version       = 0x05; // In development. See JIRA CASSANDRA-9362.
  static constexpr Version kResponseVersion = 0x80;

  using Flags = uint8_t;
  static constexpr Flags kCompressionFlag   = 0x01;
  static constexpr Flags kTracingFlag       = 0x02;
  static constexpr Flags kCustomPayloadFlag = 0x04; // Since V4
  static constexpr Flags kWarningFlag       = 0x08; // Since V4

  using StreamId = uint16_t;

  enum class Opcode : uint8_t {
    ERROR          = 0x00,
    STARTUP        = 0x01,
    READY          = 0x02,
    AUTHENTICATE   = 0x03,
    OPTIONS        = 0x05,
    SUPPORTED      = 0x06,
    QUERY          = 0x07,
    RESULT         = 0x08,
    PREPARE        = 0x09,
    EXECUTE        = 0x0A,
    REGISTER       = 0x0B,
    EVENT          = 0x0C,
    BATCH          = 0x0D,
    AUTH_CHALLENGE = 0x0E,
    AUTH_RESPONSE  = 0x0F,
    AUTH_SUCCESS   = 0x10
  };

  struct Header {
    const Version  version;
    const Flags    flags;
    const StreamId stream_id;
    const Opcode   opcode;

    Header(const Version version, const Flags flags, const StreamId stream_id, const Opcode opcode)
       : version(version), flags(flags), stream_id(stream_id), opcode(opcode) { }
    Header(const Version version, const StreamId stream_id, const Opcode opcode)
       : version(version), flags(0), stream_id(stream_id), opcode(opcode) { }
  };

  // Basic datatype mapping for CQL message body:
  //   Int        -> int32_t
  //   Long       -> int64_t
  //   Byte       -> uint8_t
  //   Short      -> uint16_t
  //   String     -> std::string
  //   LongString -> std::string
  //   UUID       -> std::string
  //   StringList -> std::vector<String>
  //   Bytes      -> std::string
  //   ShortBytes -> std::string
  //   Inet       -> Sockaddr
  //   Consistency    -> uint16_t
  //   StringMap      -> std::unordered_map<std::string, std::string>
  //   StringMultiMap -> std::unordered_map<std::string, std::vector<String>>
  //   BytesMap       -> std::unordered_map<std::string, std::string> (Since V4)
  static constexpr size_t kIntSize   = 4;
  static constexpr size_t kLongSize  = 8;
  static constexpr size_t kByteSize  = 1;
  static constexpr size_t kShortSize = 2;
  static constexpr size_t kUUIDSize  = 16;
  static constexpr size_t kIPv4Size  = 4;
  static constexpr size_t kConsistencySize = 2;
  enum class Consistency : uint16_t {
    ANY          = 0x0000,
    ONE          = 0x0001,
    TWO          = 0x0002,
    THREE        = 0x0003,
    QUORUM       = 0x0004,
    ALL          = 0x0005,
    LOCAL_QUORUM = 0x0006,
    EACH_QUORUM  = 0x0007,
    SERIAL       = 0x0008,
    LOCAL_SERIAL = 0x0009,
    LOCAL_ONE    = 0x000A
  };

  struct Value {
    enum class Kind {
      NOT_NULL =  0,
      IS_NULL  = -1, // Since V4
      NOT_SET  = -2  // Since V4
    };

    Kind kind = Kind::NOT_NULL;
    std::string name;
    std::string value;
  };

  // Query parameters for QUERY, EXECUTE and BATCH requests
  struct QueryParameters {
    using Flags = uint8_t;
    static constexpr Flags kWithValuesFlag            = 0x01;
    static constexpr Flags kSkipMetadataFlag          = 0x02;
    static constexpr Flags kWithPageSizeFlag          = 0x04;
    static constexpr Flags kWithPagingStateFlag       = 0x08;
    static constexpr Flags kWithSerialConsistencyFlag = 0x10;
    static constexpr Flags kWithDefaultTimestampFlag  = 0x20;
    static constexpr Flags kWithNamesForValuesFlag    = 0x40;

    sql::StatementParameters ToStatementParameters() const {
      if (paging_state.empty()) {
        return sql::StatementParameters(page_size);
      }
      return sql::StatementParameters(page_size, paging_state);
    }

    Consistency consistency = Consistency::ANY;
    Flags flags = 0;
    std::vector<Value> values;
    int32_t page_size = 0;
    std::string paging_state;
    Consistency serial_consistency = Consistency::ANY;
    int64_t default_timestamp = 0;
  };

  // Accessors for header fields
  Version version()    const { return header_.version;   }
  Flags flags()        const { return header_.flags;     }
  StreamId stream_id() const { return header_.stream_id; }
  Opcode opcode()      const { return header_.opcode;    }

 protected:
  explicit CQLMessage(const Header& header) : header_(header) { }
  virtual ~CQLMessage() { }

  const Header header_;
};

// ------------------------------------ CQL request -----------------------------------
class CQLRequest : public CQLMessage {
 public:
  // "Factory" function to parse a CQL serlized request message and construct a request object.
  // Return true iff a request is parsed successfully without error. If an error occurs, an error
  // response will be returned instead and it should be sent back to the CQL client.
  static bool ParseRequest(
      const Slice& mesg, std::unique_ptr<CQLRequest>* request,
      std::unique_ptr<CQLResponse>* error_response);

  virtual ~CQLRequest();

  virtual CQLResponse* Execute(CQLProcessor *processor) = 0;

 protected:
  CQLRequest(const Header& header, const Slice& body);

  // Function to parse a request body that all CQLRequest subclasses need to implement
  virtual CHECKED_STATUS ParseBody() = 0;

  // Parse a CQL number (8, 16, 32 and 64-bit integer). <num_type> is the parsed integer type.
  // <converter> converts the number from network byte-order to machine order and <data_type>
  // is the coverter's return type. The converter's return type <data_type> is unsigned while
  // <num_type> may be signed or unsigned.
  template<typename num_type, typename data_type>
  inline CHECKED_STATUS ParseNum(
      const char* type_name, data_type (*converter)(const void*), num_type* val) {
    RETURN_NOT_OK(CQLDecodeNum(sizeof(num_type), converter, &body_, val));
    DVLOG(4) << type_name << " " << static_cast<int64_t>(*val);
    return Status::OK();
  }

  // Parse a CQL byte stream (string or bytes). <len_type> is the parsed length type.
  // <len_parser> parses the byte length from network byte-order to machine order.
  template<typename len_type>
  inline CHECKED_STATUS ParseBytes(
      const char* type_name, Status (CQLRequest::*len_parser)(len_type*), std::string* val) {
    len_type len;
    RETURN_NOT_OK((this->*len_parser)(&len));
    RETURN_NOT_OK(CQLDecodeBytes(static_cast<size_t>(len), &body_, val));
    DVLOG(4) << type_name << " " << *val;
    return Status::OK();
  }

  CHECKED_STATUS ParseInt(int32_t* value);
  CHECKED_STATUS ParseLong(int64_t* value);
  CHECKED_STATUS ParseByte(uint8_t* value);
  CHECKED_STATUS ParseShort(uint16_t* value);
  CHECKED_STATUS ParseString(std::string* value);
  CHECKED_STATUS ParseLongString(std::string* value);
  CHECKED_STATUS ParseUUID(std::string* value);
  CHECKED_STATUS ParseStringList(std::vector<std::string>* list);
  CHECKED_STATUS ParseBytes(std::string* value);
  CHECKED_STATUS ParseShortBytes(std::string* value);
  CHECKED_STATUS ParseInet(Sockaddr* value);
  CHECKED_STATUS ParseConsistency(Consistency* consistency);
  CHECKED_STATUS ParseStringMap(std::unordered_map<std::string, std::string>* map);
  CHECKED_STATUS ParseStringMultiMap(
      std::unordered_map<std::string, std::vector<std::string>>* map);
  CHECKED_STATUS ParseBytesMap(std::unordered_map<std::string, std::string>* map);
  CHECKED_STATUS ParseValue(bool with_name, Value* value);
  CHECKED_STATUS ParseQueryParameters(QueryParameters* params);

 private:
  Slice body_;
};

// ------------------------------ Individual CQL requests -----------------------------------
class StartupRequest : public CQLRequest {
 public:
  StartupRequest(const Header& header, const Slice& body);
  virtual ~StartupRequest() override;
  virtual CQLResponse* Execute(CQLProcessor *processor) override;

 protected:
  virtual CHECKED_STATUS ParseBody() override;

 private:
  std::unordered_map<std::string, std::string> options_;
};

//------------------------------------------------------------
class AuthResponseRequest : public CQLRequest {
 public:
  AuthResponseRequest(const Header& header, const Slice& body);
  virtual ~AuthResponseRequest() override;
  virtual CQLResponse* Execute(CQLProcessor *processor) override;

 protected:
  virtual CHECKED_STATUS ParseBody() override;

 private:
  std::string token_;
};

//------------------------------------------------------------
class OptionsRequest : public CQLRequest {
 public:
  OptionsRequest(const Header& header, const Slice& body);
  virtual ~OptionsRequest() override;
  virtual CQLResponse* Execute(CQLProcessor *processor) override;

 protected:
  virtual CHECKED_STATUS ParseBody() override;
};

//------------------------------------------------------------
class QueryRequest : public CQLRequest {
 public:
  QueryRequest(const Header& header, const Slice& body);
  virtual ~QueryRequest() override;
  virtual CQLResponse* Execute(CQLProcessor *processor) override;

  const std::string& query() const {
    return query_;
  }
  sql::StatementParameters GetStatementParameters() const {
    return params_.ToStatementParameters();
  }

 protected:
  virtual CHECKED_STATUS ParseBody() override;

 private:
  friend class RowsResultResponse;

  std::string query_;
  QueryParameters params_;
};

//------------------------------------------------------------
class PrepareRequest : public CQLRequest {
 public:
  PrepareRequest(const Header& header, const Slice& body);
  virtual ~PrepareRequest() override;
  virtual CQLResponse* Execute(CQLProcessor *processor) override;

 protected:
  virtual CHECKED_STATUS ParseBody() override;

 private:
  std::string query_;
};

//------------------------------------------------------------
class ExecuteRequest : public CQLRequest {
 public:
  ExecuteRequest(const Header& header, const Slice& body);
  virtual ~ExecuteRequest() override;
  virtual CQLResponse* Execute(CQLProcessor *processor) override;

  const QueryParameters& params() const {
    return params_;
  }

 protected:
  virtual CHECKED_STATUS ParseBody() override;

 private:
  std::string query_id_;
  QueryParameters params_;
};

//------------------------------------------------------------
class BatchRequest : public CQLRequest {
 public:
  BatchRequest(const Header& header, const Slice& body);
  virtual ~BatchRequest() override;
  virtual CQLResponse* Execute(CQLProcessor *processor) override;

 protected:
  virtual CHECKED_STATUS ParseBody() override;

 private:
  enum class Type : uint8_t {
    LOGGED   = 0x00,
    UNLOGGED = 0x01,
    COUNTER  = 0x02
  };
  struct Query {
    bool is_prepared = false;
    std::string query_id;
    std::string query;
    std::vector<Value> values;
  };

  Type type_ = Type::LOGGED;
  std::vector<Query> queries_;
  Consistency consistency_ = Consistency::ANY;
  QueryParameters::Flags flags_ = 0;
  Consistency serial_consistency_ = Consistency::ANY;
  int64_t default_timestamp_ = 0;
};

//------------------------------------------------------------
class RegisterRequest : public CQLRequest {
 public:
  RegisterRequest(const Header& header, const Slice& body);
  virtual ~RegisterRequest() override;
  virtual CQLResponse* Execute(CQLProcessor *processor) override;

 protected:
  virtual CHECKED_STATUS ParseBody() override;

 private:
  std::vector<std::string> event_types_;
};

// ------------------------------------ CQL response -----------------------------------
class CQLResponse : public CQLMessage {
 public:
  void Serialize(faststring* mesg);
  virtual ~CQLResponse();
 protected:
  CQLResponse(const CQLRequest& request, Opcode opcode);
  CQLResponse(StreamId stream_id, Opcode opcode);
  void SerializeHeader(faststring* mesg);

  // Function to serialize a response body that all CQLResponse subclasses need to implement
  virtual void SerializeBody(faststring* mesg) = 0;

  // Serialize a CQL number (8, 16, 32 and 64-bit integer). <num_type> is the integer type.
  // <converter> converts the number from machine byte-order to network order and <data_type>
  // is the coverter's return type. The converter's return type <data_type> is unsigned while
  // <num_type> may be signed or unsigned.
  template<typename num_type, typename data_type>
  inline void SerializeNum(
      void (*converter)(void *, data_type), const num_type val, faststring* mesg) {
    data_type byte_value;
    (*converter)(&byte_value, static_cast<data_type>(val));
    mesg->append(&byte_value, sizeof(byte_value));
  }

  // Serialize a CQL byte stream (string or bytes). <len_type> is the length type.
  // <len_serializer> serializes the byte length from machine byte-order to network order.
  template<typename len_type>
  inline void SerializeBytes(
      void (CQLResponse::*len_serializer)(len_type, faststring* mesg), std::string val,
      faststring* mesg) {
    (this->*len_serializer)(static_cast<len_type>(val.size()), mesg);
    mesg->append(val);
  }

  void SerializeInt(int32_t value, faststring* mesg);
  void SerializeLong(int64_t value, faststring* mesg);
  void SerializeByte(uint8_t value, faststring* mesg);
  void SerializeShort(uint16_t value, faststring* mesg);
  void SerializeString(const std::string& value, faststring* mesg);
  void SerializeLongString(const std::string& value, faststring* mesg);
  void SerializeUUID(const std::string& value, faststring* mesg);
  void SerializeStringList(const std::vector<std::string>& list, faststring* mesg);
  void SerializeBytes(const std::string& value, faststring* mesg);
  void SerializeShortBytes(const std::string& value, faststring* mesg);
  void SerializeInet(const Sockaddr& value, faststring* mesg);
  void SerializeConsistency(Consistency value, faststring* mesg);
  void SerializeStringMap(const std::unordered_map<std::string, std::string>& map,
      faststring* mesg);
  void SerializeStringMultiMap(
      const std::unordered_map<std::string, std::vector<std::string>>& map, faststring* mesg);
  void SerializeBytesMap(const std::unordered_map<std::string, std::string>& map, faststring* mesg);
  void SerializeValue(const Value& value, faststring* mesg);
};

// ------------------------------ Individual CQL responses -----------------------------------
class ErrorResponse : public CQLResponse {
 public:
  enum class Code : int32_t {
    SERVER_ERROR     = 0x0000,
    PROTOCOL_ERROR   = 0x000A,
    BAD_CREDENTIALS  = 0x0100,
    UNAVAILABLE      = 0x1000,
    OVERLOADED       = 0x1001,
    IS_BOOTSTRAPPING = 0x1002,
    TRUNCATE_ERROR   = 0x1003,
    WRITE_TIMEOUT    = 0x1100,
    READ_TIMEOUT     = 0x1200,
    READ_FAILURE     = 0x1300, // Since V4
    FUNCTION_FAILURE = 0x1400, // Since V4
    WRITE_FAILURE    = 0x1500, // Since V4
    SYNTAX_ERROR     = 0x2000,
    UNAUTHORIZED     = 0x2100,
    INVALID          = 0x2200,
    CONFIG_ERROR     = 0x2300,
    ALREADY_EXISTS   = 0x2400,
    UNPREPARED       = 0x2500,
  };

  // Construct an error response for the request, or by just the stream id if the request object
  // is not constructed (e.g. when the request opcode is invalid).
  ErrorResponse(const CQLRequest& request, Code code, const std::string& message);
  ErrorResponse(const CQLRequest& request, Code code, const Status& status);
  ErrorResponse(StreamId stream_id, Code code, const std::string& message);

  virtual ~ErrorResponse() override;

 protected:
  virtual void SerializeBody(faststring* mesg) override;

 private:
  const Code code_;
  const std::string message_;
};

//------------------------------------------------------------
class ReadyResponse : public CQLResponse {
 public:
  virtual ~ReadyResponse() override;

 protected:
  virtual void SerializeBody(faststring* mesg) override;

 private:
  friend class StartupRequest;
  friend class RegisterRequest;

  explicit ReadyResponse(const CQLRequest& request);
};

//------------------------------------------------------------
class AuthenticateResponse : public CQLResponse {
 public:
  virtual ~AuthenticateResponse() override;

 protected:
  virtual void SerializeBody(faststring* mesg) override;

 private:
  AuthenticateResponse(const CQLRequest& request, const std::string& authenticator);

  const std::string authenticator_;
};

//------------------------------------------------------------
class SupportedResponse : public CQLResponse {
 public:
  virtual ~SupportedResponse() override;

 protected:
  virtual void SerializeBody(faststring* mesg) override;

 private:
  friend class StartupRequest;
  friend class OptionsRequest;

  explicit SupportedResponse(const CQLRequest& request);

  static const std::unordered_map<std::string, std::vector<std::string>> options_;
};

//------------------------------------------------------------
class ResultResponse : public CQLResponse {
 public:
  virtual ~ResultResponse() override;

 protected:
  enum class Kind : int32_t {
    VOID          = 0x00000001,
    ROWS          = 0x00000002,
    SET_KEYSPACE  = 0x00000003,
    PREPARED      = 0x00000004,
    SCHEMA_CHANGE = 0x00000005
  };

  // Metadata for ROWS resultset
  struct RowsMetadata {
    using Flags = int32_t;
    static constexpr Flags kHasGlobalTableSpec = 0x00000001;
    static constexpr Flags kHasMorePages       = 0x00000002;
    static constexpr Flags kNoMetadata         = 0x00000004;

    Flags flags = 0;
    std::string paging_state;

    struct GlobalTableSpec {
      std::string keyspace;
      std::string table;

      GlobalTableSpec(const std::string& keyspace, const std::string& table)
          : keyspace(keyspace), table(table) { }
    };
    GlobalTableSpec global_table_spec;

    struct Type {
      enum class Id : uint16_t {
        CUSTOM    = 0x0000,
        ASCII     = 0x0001,
        BIGINT    = 0x0002,
        BLOB      = 0x0003,
        BOOLEAN   = 0x0004,
        COUNTER   = 0x0005,
        DECIMAL   = 0x0006,
        DOUBLE    = 0x0007,
        FLOAT     = 0x0008,
        INT       = 0x0009,
        TIMESTAMP = 0x000B,
        UUID      = 0x000C,
        VARCHAR   = 0x000D,
        VARINT    = 0x000E,
        TIMEUUID  = 0x000F,
        INET      = 0x0010,
        DATE      = 0x0011, // Since V4
        TIME      = 0x0012, // Since V4
        SMALLINT  = 0x0013, // Since V4
        TINYINT   = 0x0014, // Since V4
        LIST      = 0x0020,
        MAP       = 0x0021,
        SET       = 0x0022,
        UDT       = 0x0030,
        TUPLE     = 0x0031
      };

      Id id = Id::CUSTOM;

      struct MapType {
        std::shared_ptr<const Type> key_type;
        std::shared_ptr<const Type> value_type;
      };

      struct UDTType {
        std::string keyspace;
        std::string name;
        struct Field {
          std::string name;
          std::shared_ptr<const Type> type;
        };
        std::vector<Field> fields;
      };

      typedef std::vector<std::shared_ptr<const Type>> TupleComponentTypes;

      // The use of union to store strings and shared pointers needs to be done with great care
      // since constructors and destructors won't be called automatically.
      // TODO(Robert): an alternate idea will be to create a type hierarchy of individual types
      // and allocate it out of a memory pool to avoid allocation overhead and memory
      // fragmentation. Revisit it when we return real query results from tserver.
      union {
        std::string custom_class_name;
        std::shared_ptr<const Type> element_type; // for list or set
        std::shared_ptr<const MapType> map_type;
        std::shared_ptr<const UDTType> udt_type;
        std::shared_ptr<const TupleComponentTypes> tuple_component_types;
      };

      explicit Type(Id id); // for primitive type
      explicit Type(const std::string& custom_class_name);
               Type(Id id, std::shared_ptr<const Type> element_type); // for list or set
      explicit Type(std::shared_ptr<const MapType> map_type);
      explicit Type(std::shared_ptr<const UDTType> udt_type);
      explicit Type(std::shared_ptr<const TupleComponentTypes> tuple_component_types);
      explicit Type(const Type& t);
      explicit Type(DataType type);
      ~Type();
    };
    struct ColSpec {
      std::string keyspace;
      std::string table;
      std::string column;
      Type type;

      ColSpec(std::string column, const Type& type)
          : keyspace(""), table(""), column(column), type(type) {}
    };
    int32_t col_count;
    std::vector<ColSpec> col_specs;

    explicit RowsMetadata(const client::YBTableName& table_name,
                          const std::vector<ColumnSchema>& columns,
                          const std::string& paging_state,
                          bool no_metadata);
  };

  ResultResponse(const CQLRequest& request, Kind kind);
  virtual void SerializeBody(faststring* mesg) override;

  // Function to serialize a result body that all ResultResponse subclasses need to implement
  virtual void SerializeResultBody(faststring* mesg) = 0;

  // Helper serialize functions
  void SerializeType(const RowsMetadata::Type* type, faststring* mesg);
  void SerializeColSpecs(
      bool has_global_table_spec, const RowsMetadata::GlobalTableSpec& global_table_spec,
      const std::vector<RowsMetadata::ColSpec>& col_specs, faststring* mesg);
  void SerializeRowsMetadata(const RowsMetadata& metadata, faststring* mesg);

 private:
  const Kind kind_;
};

//------------------------------------------------------------
class VoidResultResponse : public ResultResponse {
 public:
  explicit VoidResultResponse(const CQLRequest& request);
  virtual ~VoidResultResponse() override;

 protected:
  virtual void SerializeResultBody(faststring* mesg) override;
};

//------------------------------------------------------------
class RowsResultResponse : public ResultResponse {
 public:
  RowsResultResponse(const QueryRequest& request, const sql::RowsResult& rows_result);
  virtual ~RowsResultResponse() override;

 protected:
  virtual void SerializeResultBody(faststring* mesg) override;

 private:
  const sql::RowsResult& rows_result_;
  const bool skip_metadata_;
};

//------------------------------------------------------------
class SetKeyspaceResultResponse : public ResultResponse {
 public:
  SetKeyspaceResultResponse(const CQLRequest& request, const std::string& keyspace);
  virtual ~SetKeyspaceResultResponse() override;
 protected:
  virtual void SerializeResultBody(faststring* mesg) override;
 private:
  const std::string keyspace_;
};

//------------------------------------------------------------
class PreparedResultResponse : public ResultResponse {
 public:
  virtual ~PreparedResultResponse() override;

 protected:
  virtual void SerializeResultBody(faststring* mesg) override;

 private:
  struct PreparedMetadata {
    using Flags = int32_t;
    static constexpr Flags kHasGlobalTableSpec = 0x00000001;

    Flags flags = 0;
    std::vector<uint16_t> pk_indices;
    RowsMetadata::GlobalTableSpec global_table_spec;
    std::vector<RowsMetadata::ColSpec> col_specs;
  };

  PreparedResultResponse(
      const CQLRequest& request, const PreparedMetadata& prepared_metadata,
      const RowsMetadata& rows_metadata);
  void SerializePreparedMetadata(const PreparedMetadata& metadata, faststring* mesg);

  const std::string query_id_;
  const PreparedMetadata prepared_metadata_;
  const RowsMetadata rows_metadata_;
};

//------------------------------------------------------------
class SchemaChangeResultResponse : public ResultResponse {
 public:
  virtual ~SchemaChangeResultResponse() override;

 protected:
  virtual void SerializeResultBody(faststring* mesg) override;

 private:
  static const std::vector<std::string> kEmptyArgumentTypes;

  SchemaChangeResultResponse(
      const CQLRequest& request, const std::string& change_type, const std::string& target,
      const std::string& keyspace, const std::string& object = "",
      const std::vector<std::string>& argument_types = kEmptyArgumentTypes);

  const std::string change_type_;
  const std::string target_;
  const std::string keyspace_;
  const std::string object_;
  const std::vector<std::string> argument_types_;
};

//------------------------------------------------------------
class EventResponse : public CQLResponse {
 protected:
  EventResponse(const CQLRequest& request, const std::string& event_type);
  virtual ~EventResponse() override;
  virtual void SerializeBody(faststring* mesg) override;
  virtual void SerializeEventBody(faststring* mesg) = 0;
 private:
  const std::string event_type_;
};

//------------------------------------------------------------
class TopologyChangeEventResponse : public EventResponse {
 public:
  virtual ~TopologyChangeEventResponse() override;

 protected:
  virtual void SerializeEventBody(faststring* mesg) override;

 private:
  TopologyChangeEventResponse(
      const CQLRequest& request, const std::string& topology_change_type, const Sockaddr& node);

  const std::string topology_change_type_;
  const Sockaddr node_;
};

//------------------------------------------------------------
class StatusChangeEventResponse : public EventResponse {
 public:
  virtual ~StatusChangeEventResponse() override;

 protected:
  virtual void SerializeEventBody(faststring* mesg) override;

 private:
  StatusChangeEventResponse(
      const CQLRequest& request, const std::string& status_change_type, const Sockaddr& node);

  const std::string status_change_type_;
  const Sockaddr node_;
};

//------------------------------------------------------------
class SchemaChangeEventResponse : public EventResponse {
 public:
  virtual ~SchemaChangeEventResponse() override;

 protected:
  virtual void SerializeEventBody(faststring* mesg) override;

 private:
  static const std::vector<std::string> kEmptyArgumentTypes;

  SchemaChangeEventResponse(
      const CQLRequest& request, const std::string& schema_change_type, const std::string& target,
      const std::string& keyspace, const std::string& object = "",
      const std::vector<std::string>& argument_types = kEmptyArgumentTypes);

  const std::string schema_change_type_;
  const std::string target_;
  const std::string keyspace_;
  const std::string object_;
  const std::vector<std::string> argument_types_;
};

//------------------------------------------------------------
class AuthChallengeResponse : public CQLResponse {
 public:
  virtual ~AuthChallengeResponse() override;

 protected:
  virtual void SerializeBody(faststring* mesg) override;

 private:
  AuthChallengeResponse(const CQLRequest& request, const std::string& token);

  const std::string token_;
};

//------------------------------------------------------------
class AuthSuccessResponse : public CQLResponse {
 public:
  virtual ~AuthSuccessResponse() override;

 protected:
  virtual void SerializeBody(faststring* mesg) override;

 private:
  AuthSuccessResponse(const CQLRequest& request, const std::string& token);

  const std::string token_;
};

} // namespace cqlserver
} // namespace yb

#endif // YB_CQLSERVER_CQL_MESSAGE_H
