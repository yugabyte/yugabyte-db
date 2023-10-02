//--------------------------------------------------------------------------------------------------
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
//--------------------------------------------------------------------------------------------------

#include "yb/bfql/directory.h"

#include "yb/common/value.messages.h"

namespace yb {
namespace bfql {

using std::vector;

const DataType VOID = DataType::UNKNOWN_DATA;
const DataType ANYTYPE = DataType::NULL_VALUE_TYPE;
const DataType INT8 = DataType::INT8;
const DataType INT16 = DataType::INT16;
const DataType INT32 = DataType::INT32;
const DataType INT64 = DataType::INT64;
const DataType STRING = DataType::STRING;
const DataType BOOL = DataType::BOOL;
const DataType FLOAT = DataType::FLOAT;
const DataType DOUBLE = DataType::DOUBLE;
const DataType BINARY = DataType::BINARY;
const DataType TIMESTAMP = DataType::TIMESTAMP;
const DataType DECIMAL = DataType::DECIMAL;
const DataType VARINT = DataType::VARINT;
const DataType INET = DataType::INET;
const DataType LIST = DataType::LIST;
const DataType MAP = DataType::MAP;
const DataType SET = DataType::SET;
const DataType UUID = DataType::UUID;
const DataType TIMEUUID = DataType::TIMEUUID;
const DataType TUPLE = DataType::TUPLE;
const DataType TYPEARGS = DataType::TYPEARGS;
const DataType DATE = DataType::DATE;
const DataType TIME = DataType::TIME;
const DataType JSONB = DataType::JSONB;
const DataType FROZEN = DataType::FROZEN;
const DataType UDT = DataType::USER_DEFINED_TYPE;

// IMPORTANT NOTES:
// - If your cpp_function is defined in a different name space, you must "#include" its header file
//   in "bfql/directory.h" and enter the function full name here (such as XX::YY::func_name).
// - All yql function names in this table MUST be in lower case.
// - New entry must be added at the end for backward compatibility reason. We use the order of
//   this table to generate OPCODE, so changing the order might end up loading wrong operator for
//   a given OPCODE when connecting with server of older release version.
// - See "directory.h" header file for more explanation.
//
// Left to right: cpp_name, ql_name, return_type, argument_types.
const vector<BFDecl> kBFDirectory = {
  // Add this no op entry at the begining just in case we need a MIN value for operators.
  { "NoOp", "NO_OP", "", VOID, {} },

  //------------------------------------------------------------------------------------------------
  // Conversion routines.
  // - The following code block defines the first set of primitive conversion routines.
  // - Do not add newly-introduced conversion routines here. Because the order of this table is
  //   used to generate OPCODE enum, whose values must be the same for the lifetime of YugaByte
  //   for compatibility reasons, all new entries must be added at the end.

  // Numeric conversion.
  { "ConvertI8ToI16",       "cast", "", INT16,  {INT8, INT16} },
  { "ConvertI8ToI32",       "cast", "", INT32,  {INT8, INT32} },
  { "ConvertI8ToI64",       "cast", "", INT64,  {INT8, INT64} },
  { "ConvertI8ToFloat",     "cast", "", FLOAT,  {INT8, FLOAT} },
  { "ConvertI8ToDouble",    "cast", "", DOUBLE, {INT8, DOUBLE} },

  { "ConvertI16ToI8",       "cast", "", INT8,   {INT16, INT8} },
  { "ConvertI16ToI32",      "cast", "", INT32,  {INT16, INT32} },
  { "ConvertI16ToI64",      "cast", "", INT64,  {INT16, INT64} },
  { "ConvertI16ToFloat",    "cast", "", FLOAT,  {INT16, FLOAT} },
  { "ConvertI16ToDouble",   "cast", "", DOUBLE, {INT16, DOUBLE} },

  { "ConvertI32ToI8",       "cast", "", INT8,   {INT32, INT8} },
  { "ConvertI32ToI16",      "cast", "", INT16,  {INT32, INT16} },
  { "ConvertI32ToI64",      "cast", "", INT64,  {INT32, INT64} },
  { "ConvertI32ToFloat",    "cast", "", FLOAT,  {INT32, FLOAT} },
  { "ConvertI32ToDouble",   "cast", "", DOUBLE, {INT32, DOUBLE} },

  { "ConvertI64ToI8",       "cast", "", INT8,   {INT64, INT8} },
  { "ConvertI64ToI16",      "cast", "", INT16,  {INT64, INT16} },
  { "ConvertI64ToI32",      "cast", "", INT32,  {INT64, INT32} },
  { "ConvertI64ToFloat",    "cast", "", FLOAT,  {INT64, FLOAT} },
  { "ConvertI64ToDouble",   "cast", "", DOUBLE, {INT64, DOUBLE} },

  { "ConvertDoubleToFloat", "cast", "", FLOAT,  {DOUBLE, FLOAT} },
  { "ConvertFloatToDouble", "cast", "", DOUBLE, {FLOAT, DOUBLE} },

    // CQL functions "TypeAsBlob".
  { "ConvertStringToBlob",    "varcharasblob", "",   BINARY, {STRING}, TSOpcode::kNoOp },
  { "ConvertStringToBlob",    "textasblob", "",      BINARY, {STRING}, TSOpcode::kNoOp },

  { "ConvertBoolToBlob",      "booleanasblob", "",   BINARY, {BOOL}, TSOpcode::kNoOp },

  { "ConvertInt8ToBlob",      "tinyintasblob", "",   BINARY, {INT8}, TSOpcode::kNoOp },
  { "ConvertInt16ToBlob",     "smallintasblob", "",  BINARY, {INT16}, TSOpcode::kNoOp },
  { "ConvertInt32ToBlob",     "intasblob", "",       BINARY, {INT32}, TSOpcode::kNoOp },
  { "ConvertInt64ToBlob",     "bigintasblob", "",    BINARY, {INT64}, TSOpcode::kNoOp },
  { "ConvertInt64ToBlob",     "counterasblob", "",   BINARY, {INT64}, TSOpcode::kNoOp },
  { "ConvertVarintToBlob",    "varintasblob", "",    BINARY, {VARINT}, TSOpcode::kNoOp, false },

  { "ConvertFloatToBlob",     "floatasblob", "",     BINARY, {FLOAT}, TSOpcode::kNoOp },
  { "ConvertDoubleToBlob",    "doubleasblob", "",    BINARY, {DOUBLE}, TSOpcode::kNoOp },
  { "ConvertDecimalToBlob",   "decimalasblob", "",   BINARY, {DECIMAL}, TSOpcode::kNoOp, false },

  { "ConvertDateToBlob",      "dateasblob", "",      BINARY, {DATE}, TSOpcode::kNoOp, false },
  { "ConvertTimeToBlob",      "timeasblob", "",      BINARY, {TIME}, TSOpcode::kNoOp, false },
  { "ConvertTimestampToBlob", "timestampasblob", "", BINARY, {TIMESTAMP}, TSOpcode::kNoOp },
  { "ConvertUuidToBlob",      "uuidasblob", "",      BINARY, {UUID}, TSOpcode::kNoOp },
  { "ConvertTimeuuidToBlob",  "timeuuidasblob", "",  BINARY, {TIMEUUID}, TSOpcode::kNoOp },
  { "ConvertInetToBlob",      "inetasblob", "",      BINARY, {INET}, TSOpcode::kNoOp, false },

  { "ConvertListToBlob",      "listasblob", "",      BINARY, {LIST}, TSOpcode::kNoOp, false },
  { "ConvertMapToBlob",       "mapasblob", "",       BINARY, {MAP}, TSOpcode::kNoOp, false },
  { "ConvertSetToBlob",       "setasblob", "",       BINARY, {SET}, TSOpcode::kNoOp, false },
  { "ConvertTupleToBlob",     "tupleasblob", "",     BINARY, {TUPLE}, TSOpcode::kNoOp, false },

    // CQL functions "BlobAsType".
  { "ConvertBlobToString",    "blobasvarchar", "",   STRING,    {BINARY}, TSOpcode::kNoOp },
  { "ConvertBlobToString",    "blobastext", "",      STRING,    {BINARY}, TSOpcode::kNoOp },

  { "ConvertBlobToBool",      "blobasboolean", "",   BOOL,      {BINARY}, TSOpcode::kNoOp },

  { "ConvertBlobToInt8",      "blobastinyint", "",   INT8,      {BINARY}, TSOpcode::kNoOp },
  { "ConvertBlobToInt16",     "blobassmallint", "",  INT16,     {BINARY}, TSOpcode::kNoOp },
  { "ConvertBlobToInt32",     "blobasint", "",       INT32,     {BINARY}, TSOpcode::kNoOp },
  { "ConvertBlobToInt64",     "blobasbigint", "",    INT64,     {BINARY}, TSOpcode::kNoOp },
  { "ConvertBlobToInt64",     "blobascounter", "",   INT64,     {BINARY}, TSOpcode::kNoOp },
  { "ConvertBlobToVarint",    "blobasvarint", "",    VARINT,    {BINARY}, TSOpcode::kNoOp, false },

  { "ConvertBlobToFloat",     "blobasfloat", "",     FLOAT,     {BINARY}, TSOpcode::kNoOp },
  { "ConvertBlobToDouble",    "blobasdouble", "",    DOUBLE,    {BINARY}, TSOpcode::kNoOp },
  { "ConvertBlobToDecimal",   "blobasdecimal", "",   DECIMAL,   {BINARY}, TSOpcode::kNoOp, false },

  { "ConvertBlobToDate",      "blobasdate", "",      DATE,      {BINARY}, TSOpcode::kNoOp, false },
  { "ConvertBlobToTime",      "blobastime", "",      TIME,      {BINARY}, TSOpcode::kNoOp, false },
  { "ConvertBlobToTimestamp", "blobastimestamp", "", TIMESTAMP, {BINARY}, TSOpcode::kNoOp },
  { "ConvertBlobToUuid",      "blobasuuid", "",      UUID,      {BINARY}, TSOpcode::kNoOp },
  { "ConvertBlobToTimeuuid",  "blobastimeuuid", "",  TIMEUUID,  {BINARY}, TSOpcode::kNoOp },
  { "ConvertBlobToInet",      "blobasinet", "",      INET,      {BINARY}, TSOpcode::kNoOp, false },

  { "ConvertBlobToList",      "blobaslist", "",      LIST,      {BINARY}, TSOpcode::kNoOp, false },
  { "ConvertBlobToMap",       "blobasmap", "",       MAP,       {BINARY}, TSOpcode::kNoOp, false },
  { "ConvertBlobToSet",       "blobasset", "",       SET,       {BINARY}, TSOpcode::kNoOp, false },
  { "ConvertBlobToTuple",     "blobastuple", "",     TUPLE,     {BINARY}, TSOpcode::kNoOp, false },

  // CQL Conversions for TimeUUID and date-time types.
  { "ConvertTimeuuidToDate", "todate", "", DATE, {TIMEUUID}, TSOpcode::kNoOp},
  { "ConvertTimestampToDate", "todate", "", DATE, {TIMESTAMP}, TSOpcode::kNoOp},

  { "ConvertTimeuuidToTime", "totime", "", TIME, {TIMEUUID}, TSOpcode::kNoOp, false },
  { "ConvertTimestampToTime", "totime", "", TIME, {TIMESTAMP}, TSOpcode::kNoOp, false },

  { "ConvertDateToTimestamp", "totimestamp", "", TIMESTAMP, {DATE}, TSOpcode::kNoOp},
  { "ConvertTimeuuidToTimestamp", "totimestamp", "", TIMESTAMP, {TIMEUUID}, TSOpcode::kNoOp},
  { "ConvertTimeuuidToTimestamp", "dateof", "", TIMESTAMP, {TIMEUUID}, TSOpcode::kNoOp},

  { "ConvertDateToUnixTimestamp", "tounixtimestamp", "", INT64, {DATE}, TSOpcode::kNoOp},
  { "ConvertTimestampToUnixTimestamp", "tounixtimestamp", "", INT64, {TIMESTAMP}, TSOpcode::kNoOp},
  { "ConvertTimeuuidToUnixTimestamp", "tounixtimestamp", "", INT64, {TIMEUUID}, TSOpcode::kNoOp},
  { "ConvertTimeuuidToUnixTimestamp", "unixtimestampof", "", INT64, {TIMEUUID}, TSOpcode::kNoOp},

  // Converting date-time literals.
  { "ConvertToMaxTimeuuid", "maxtimeuuid", "", TIMEUUID, {TIMESTAMP}, TSOpcode::kNoOp },
  { "ConvertToMinTimeuuid", "mintimeuuid", "", TIMEUUID, {TIMESTAMP}, TSOpcode::kNoOp },

  //------------------------------------------------------------------------------------------------
  // CQL standard functions.
  // "+".
  { "AddI64I64", "+", "", INT64, {INT64, INT64} },
  { "AddDoubleDouble", "+", "", DOUBLE, {DOUBLE, DOUBLE} },
  { "AddStringString", "+", "", STRING, {STRING, STRING} },
  { "AddStringDouble", "+", "", STRING, {STRING, DOUBLE} },
  { "AddDoubleString", "+", "", STRING, {DOUBLE, STRING} },

  // "-".
  { "SubI64I64", "-", "", INT64, {INT64, INT64} },
  { "SubDoubleDouble", "-", "", DOUBLE, {DOUBLE, DOUBLE} },

  // Collection functions.
  { "ServerOperator", "map+", "", MAP, {MAP, MAP}, TSOpcode::kMapExtend },
  { "ServerOperator", "map-", "OPCODE_MAP_REMOVE", MAP, {MAP, SET}, TSOpcode::kMapRemove },

  { "ServerOperator", "set+", "", SET, {SET, SET}, TSOpcode::kSetExtend },
  { "ServerOperator", "set-", "", SET, {SET, SET}, TSOpcode::kSetRemove },

  { "ServerOperator", "list+", "OPCODE_LIST_APPEND", LIST, {LIST, LIST}, TSOpcode::kListAppend },
  { "ServerOperator", "+list", "OPCODE_LIST_PREPEND", LIST, {LIST, LIST}, TSOpcode::kListPrepend },
  { "ServerOperator", "list-", "OPCODE_LIST_REMOVE", LIST, {LIST, LIST}, TSOpcode::kListRemove },

  // Token().
  { "Token", "token", "", INT64, {TYPEARGS} },
  // partition_hash(). partition_hash returns a uint16, but since we only support signed
  // integers, we use INT32 here instead.
  { "PartitionHash", "partition_hash", "", INT32, {TYPEARGS} },

  // Counter functions.
  { "IncCounter", "counter+", "", INT64, {INT64, INT64} },
  { "DecCounter", "counter-", "", INT64, {INT64, INT64} },

  // Now function.
  { "NowTimeUuid", "now", "", TIMEUUID, {} },

  // WRITETIME and TTL functions.
  // Aggregate functions has TSERVER_OPCODE to instruct tablet server what should be done.
  { "ServerOperator", "writetime", "", INT64, {ANYTYPE}, TSOpcode::kWriteTime },
  { "ServerOperator", "ttl", "", INT64, {ANYTYPE}, TSOpcode::kTtl },

  // Aggregate functions.
  // - Have TSERVER_OPCODE to instruct tablet server how to execute these calls.
  // - SUM and AVG only take numeric arguments.
  // - MIN and MAX can take arguments of any types.
  { "ServerOperator", "count", "", INT64, {ANYTYPE}, TSOpcode::kCount },

  // Cassandra behavior: SUM() has exactly the same datatype as the input argument's type.
  { "ServerOperator", "sum", "", INT8, {INT8}, TSOpcode::kSum },
  { "ServerOperator", "sum", "", INT16, {INT16}, TSOpcode::kSum },
  { "ServerOperator", "sum", "", INT32, {INT32}, TSOpcode::kSum },
  { "ServerOperator", "sum", "", INT64, {INT64}, TSOpcode::kSum },
  { "ServerOperator", "sum", "", FLOAT, {FLOAT}, TSOpcode::kSum },
  { "ServerOperator", "sum", "", DOUBLE, {DOUBLE}, TSOpcode::kSum },
  { "ServerOperator", "sum", "", VARINT, {VARINT}, TSOpcode::kSum},
  { "ServerOperator", "sum", "", DECIMAL, {DECIMAL}, TSOpcode::kSum},

  // Cassandra behavior: AVG() returns SUM and COUNT as a list to be aggregated in Executor::EvalAVG
  { "ServerOperator", "avg", "", INT8, {INT8}, TSOpcode::kAvg },
  { "ServerOperator", "avg", "", INT16, {INT16}, TSOpcode::kAvg },
  { "ServerOperator", "avg", "", INT32, {INT32}, TSOpcode::kAvg },
  { "ServerOperator", "avg", "", INT64, {INT64}, TSOpcode::kAvg },
  { "ServerOperator", "avg", "", FLOAT, {FLOAT}, TSOpcode::kAvg },
  { "ServerOperator", "avg", "", DOUBLE, {DOUBLE}, TSOpcode::kAvg },
  { "ServerOperator", "avg", "", VARINT, {VARINT}, TSOpcode::kAvg, false },
  { "ServerOperator", "avg", "", DECIMAL, {DECIMAL}, TSOpcode::kAvg, false },

  { "ServerOperator", "min", "", ANYTYPE, {ANYTYPE}, TSOpcode::kMin },
  { "ServerOperator", "max", "", ANYTYPE, {ANYTYPE}, TSOpcode::kMax },

  { "ConvertVarintToI8",       "cast", "", INT8,   {VARINT, INT8} },
  { "ConvertVarintToI16",      "cast", "", INT16,  {VARINT, INT16} },
  { "ConvertVarintToI32",      "cast", "", INT32,  {VARINT, INT32} },
  { "ConvertVarintToI64",      "cast", "", INT64,  {VARINT, INT64} },
  { "ConvertVarintToFloat",    "cast", "", FLOAT,  {VARINT, FLOAT} },
  { "ConvertVarintToDouble",   "cast", "", DOUBLE, {VARINT, DOUBLE} },

  { "ConvertI8ToVarint",       "cast", "", VARINT,  {INT8, VARINT}  },
  { "ConvertI16ToVarint",      "cast", "", VARINT,  {INT16, VARINT} },
  { "ConvertI32ToVarint",      "cast", "", VARINT,  {INT32, VARINT} },
  { "ConvertI64ToVarint",      "cast", "", VARINT,  {INT64, VARINT} },
  { "ConvertToI16",            "cql_cast", "", INT16,     {ANYTYPE, INT16} },
  { "ConvertToI32",            "cql_cast", "", INT32,     {ANYTYPE, INT32} },
  { "ConvertToI64",            "cql_cast", "", INT64,     {ANYTYPE, INT64} },
  { "ConvertToDouble",         "cql_cast", "", DOUBLE,    {ANYTYPE, DOUBLE} },
  { "ConvertToFloat",          "cql_cast", "", FLOAT,     {ANYTYPE, FLOAT} },
  { "ConvertToDecimal",        "cql_cast", "", DECIMAL,   {ANYTYPE, DECIMAL} },
  { "ConvertToString",         "cql_cast", "", STRING,    {ANYTYPE, STRING} },
  { "ConvertToTimestamp",      "cql_cast", "", TIMESTAMP, {ANYTYPE, TIMESTAMP} },
  { "ConvertToDate",           "cql_cast", "", DATE,      {ANYTYPE, DATE} },

  // Current date, time, timestamp and timeuuid functions.
  { "NowDate",      "currentdate", "",      DATE, {} },
  { "NowTime",      "currenttime", "",      TIME, {} },
  { "NowTimestamp", "currenttimestamp", "", TIMESTAMP, {} },
  { "NowTimeUuid",  "currenttimeuuid", "",  TIMEUUID, {} }, // alias of now() for timeuuid.

  // uuid function.
  { "GetUuid", "uuid", "", UUID, {} },

  // tojson function.
  // tojson() for all elementary types and for simple collections - on client side.
  { "ToJson", "tojson", "", JSONB, {ANYTYPE} },
  // tojson() for parametric types (UDT, FROZEN, LIST<FROZEN>, MAP<FROZEN>, etc.) - on server side.
  { "ServerOperator", "server_tojson", "OPCODE_LIST_TO_JSON", JSONB, { LIST }, TSOpcode::kToJson },
  { "ServerOperator", "server_tojson", "OPCODE_SET_TO_JSON", JSONB, { SET }, TSOpcode::kToJson },
  { "ServerOperator", "server_tojson", "OPCODE_MAP_TO_JSON", JSONB, { MAP }, TSOpcode::kToJson },
  { "ServerOperator", "server_tojson", "OPCODE_UDT_TO_JSON", JSONB, { UDT }, TSOpcode::kToJson },
  { "ServerOperator", "server_tojson", "OPCODE_TUPLE_TO_JSON", JSONB, { TUPLE },
    TSOpcode::kToJson },
  { "ServerOperator", "server_tojson", "OPCODE_FROZEN_TO_JSON", JSONB, { FROZEN },
    TSOpcode::kToJson },

  // Collection and UDT constructors.
  { "MapConstructor", "Map::Map", "OPCODE_MAP_CONSTRUCTOR", MAP, { TYPEARGS } },
  { "SetConstructor", "Set::Set", "OPCODE_SET_CONSTRUCTOR", SET, { TYPEARGS } },
  { "ListConstructor", "List::List", "OPCODE_LIST_CONSTRUCTOR", LIST, { TYPEARGS } },
  { "MapConstructor", "Udt::Udt", "OPCODE_UDT_CONSTRUCTOR", MAP, { TYPEARGS } },

  // Collection and UDT frozen constructor.
  { "MapFrozen", "Map::Frozen", "OPCODE_MAP_FROZEN", FROZEN, { TYPEARGS } },
  { "SetFrozen", "Set::Frozen", "OPCODE_SET_FROZEN", FROZEN, { TYPEARGS } },
  { "ListFrozen", "List::Frozen", "OPCODE_LIST_FROZEN", FROZEN, { TYPEARGS } },
  { "ListFrozen", "Udt::Frozen", "OPCODE_UDT_FROZEN", FROZEN, { TYPEARGS } },
};

} // namespace bfql
} // namespace yb
