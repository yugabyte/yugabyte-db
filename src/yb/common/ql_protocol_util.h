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

#include <boost/preprocessor/cat.hpp>
#include <boost/preprocessor/seq/for_each.hpp>

#include "yb/common/common_fwd.h"
#include "yb/common/common_types.pb.h"
#include "yb/common/ql_protocol.pb.h"
#include "yb/common/value.pb.h"

#include "yb/util/status.h"
#include "yb/util/status_format.h"
#include "yb/util/write_buffer.h"
#include "yb/util/yb_partition.h"

namespace yb {

#define QL_PROTOCOL_TYPES \
    ((Int8, int8, int8_t)) \
    ((Int16, int16, int16_t)) \
    ((Int32, int32, int32_t)) \
    ((Int64, int64, int64_t)) \
    ((String, string, const std::string&)) \
    ((Binary, binary, const std::string&)) \
    ((Bool, bool, bool)) \
    ((Float, float, float)) \
    ((Double, double, double)) \
    ((Jsonb, jsonb, const std::string&)) \
    ((Timestamp, timestamp, int64_t)) \

#define PP_CAT3(a, b, c) BOOST_PP_CAT(a, BOOST_PP_CAT(b, c))

#define QL_PROTOCOL_TYPE_DECLARATIONS_IMPL(name, lname, type) \
void PP_CAT3(QLAdd, name, ColumnValue)( \
    QLWriteRequestPB* req, int column_id, type value); \
\
void PP_CAT3(QLSet, name, Expression)(QLExpressionPB *expr, type value); \
\
template <class RequestPB> \
void PP_CAT3(QLAdd, name, HashValue)(RequestPB* req, type value) { \
  PP_CAT3(QLSet, name, Expression)(req->add_hashed_column_values(), value); \
} \
template <class RequestPB> \
void PP_CAT3(QLAdd, name, RangeValue)(RequestPB* req, type value) { \
  PP_CAT3(QLSet, name, Expression)(req->add_range_column_values(), value); \
} \
void PP_CAT3(QLSet, name, Condition)( \
    QLConditionPB* condition, int column_id, QLOperator op, type value); \
\
void PP_CAT3(QLAdd, name, Condition)( \
    QLConditionPB* condition, int column_id, QLOperator op, type value); \

#define QL_PROTOCOL_TYPE_DECLARATIONS(i, data, entry) QL_PROTOCOL_TYPE_DECLARATIONS_IMPL entry

BOOST_PP_SEQ_FOR_EACH(QL_PROTOCOL_TYPE_DECLARATIONS, ~, QL_PROTOCOL_TYPES);

void QLAddNullColumnValue(QLWriteRequestPB* req, int column_id);

template <class RequestPB>
void QLSetHashCode(RequestPB* req) {
  std::string tmp;
  for (const auto& column : req->hashed_column_values()) {
    AppendToKey(column.value(), &tmp);
  }
  req->set_hash_code(YBPartition::HashColumnCompoundValue(tmp));
}

QLValuePB* QLPrepareColumn(QLWriteRequestPB* req, int column_id);
QLValuePB* QLPrepareCondition(QLConditionPB* condition, int column_id, QLOperator op);

void QLAddColumns(const Schema& schema, const std::vector<ColumnId>& columns,
                  QLReadRequestPB* req);

// Does this write request require reading existing data for evaluating expressions before writing?
bool RequireReadForExpressions(const QLWriteRequestPB& request);

// Does this write request require reading existing data in general before writing?
bool RequireRead(const QLWriteRequestPB& request, const Schema& schema);

// Does this write request perform a range operation (e.g. range delete)?
bool IsRangeOperation(const QLWriteRequestPB& request, const Schema& schema);

#define RETURN_NOT_ENOUGH(data, sz)                         \
  do {                                                      \
    if (data->size() < (sz)) {                              \
      return STATUS(NetworkError, "Truncated CQL message"); \
    }                                                       \
  } while (0)

// Decode a CQL number (8, 16, 32 and 64-bit integer). <num_type> is the parsed integer type.
// <converter> converts the number from network byte-order to machine order and <data_type>
// is the coverter's return type. The converter's return type <data_type> is unsigned while
// <num_type> may be signed or unsigned.
template<typename num_type, typename data_type>
static inline Status CQLDecodeNum(
    const size_t len, data_type (*converter)(const void*), Slice* data, num_type* val) {

  static_assert(sizeof(data_type) == sizeof(num_type), "inconsistent num type size");
  if (len != sizeof(num_type)) {
    return STATUS_SUBSTITUTE(NetworkError,
                             "unexpected number byte length: expected $0, provided $1",
                             static_cast<int64_t>(sizeof(num_type)), len);
  }

  RETURN_NOT_ENOUGH(data, sizeof(num_type));
  *val = static_cast<num_type>((*converter)(data->data()));
  data->remove_prefix(sizeof(num_type));
  return Status::OK();
}

// Decode a CQL floating point number (float or double). <float_type> is the parsed floating point
// type. <converter> converts the number from network byte-order to machine order and <data_type>
// is the coverter's return type. The converter's return type <data_type> is an integer type.
template<typename float_type, typename data_type>
static inline Status CQLDecodeFloat(
    const size_t len, data_type (*converter)(const void*), Slice* data, float_type* val) {
  // Make sure float and double are exactly sizeof uint32_t and uint64_t.
  static_assert(sizeof(float_type) == sizeof(data_type), "inconsistent floating point type size");
  data_type bval = 0;
  RETURN_NOT_OK(CQLDecodeNum(len, converter, data, &bval));
  *val = *reinterpret_cast<float_type*>(&bval);
  return Status::OK();
}

static inline Status CQLDecodeBytes(size_t len, Slice* data, std::string* val) {
  RETURN_NOT_ENOUGH(data, len);
  val->assign(data->cdata(), len);
  data->remove_prefix(len);
  return Status::OK();
}

Result<int32_t> CQLDecodeLength(Slice* data);

// Decode a 32-bit length from the buffer without consuming the buffer. Caller should ensure the
// buffer size is at least 4 bytes.
static inline int32_t CQLDecodeLength(const void* buffer) {
  return static_cast<int32_t>(NetworkByteOrder::Load32(buffer));
}

//----------------------------------- CQL value encode functions ---------------------------------
void CQLEncodeLength(const ssize_t length, WriteBuffer* buffer);

// Encode a 32-bit length into the buffer without extending the buffer. Caller should ensure the
// buffer size is at least 4 bytes.
void CQLEncodeLength(const ssize_t length, void* buffer);

// Encode a CQL number (8, 16, 32 and 64-bit integer). <num_type> is the integer type.
// <converter> converts the number from machine byte-order to network order and <data_type>
// is the coverter's return type. The converter's input type <data_type> is unsigned while
// <num_type> may be signed or unsigned.
template<typename num_type, typename data_type>
inline void CQLEncodeNum(
    void (*converter)(void *, data_type), const num_type val, WriteBuffer* buffer) {
  static_assert(sizeof(data_type) == sizeof(num_type), "inconsistent num type size");
  CQLEncodeLength(sizeof(num_type), buffer);
  data_type byte_value;
  (*converter)(&byte_value, static_cast<data_type>(val));
  buffer->Append(pointer_cast<const char*>(&byte_value), sizeof(byte_value));
}

// Encode a CQL floating point number (float or double). <float_type> is the floating point type.
// <converter> converts the number from machine byte-order to network order and <data_type>
// is the coverter's input type. The converter's input type <data_type> is an integer type.
template<typename float_type, typename data_type>
inline void CQLEncodeFloat(
    void (*converter)(void *, data_type), const float_type val, WriteBuffer* buffer) {
  static_assert(sizeof(float_type) == sizeof(data_type), "inconsistent floating point type size");
  const data_type value = *reinterpret_cast<const data_type*>(&val);
  CQLEncodeNum(converter, value, buffer);
}

inline void CQLEncodeBytes(const std::string& val, WriteBuffer* buffer) {
  CQLEncodeLength(val.size(), buffer);
  buffer->Append(Slice(val));
}

//--------------------------------------------------------------------------------------------------
// For collections the serialized length (size in bytes -- not number of elements) depends on the
// size of their (possibly variable-length) elements so cannot be pre-computed efficiently.
// Therefore CQLStartCollection and CQLFinishCollection should be called before and, respectively,
// after serializing collection elements to set the correct value

// Allocates the space in the buffer for writing the correct length later and returns the buffer
// position after (i.e. where the serialization for the collection value will begin)
inline WriteBufferPos CQLStartCollection(WriteBuffer* buffer) {
  auto result = buffer->Position();
  CQLEncodeLength(0, buffer);
  return result;
}

// Sets the value for the serialized size of a collection by subtracting the start position to
// compute length and writing it at the right position in the buffer
void CQLFinishCollection(const WriteBufferPos& start_pos, WriteBuffer* buffer);

static inline void Store8(void* p, uint8_t v) {
  *static_cast<uint8_t*>(p) = v;
}

//----------------------------------- CQL value decode functions ---------------------------------
static inline uint8_t Load8(const void* p) {
  return *static_cast<const uint8_t*>(p);
}

} // namespace yb
