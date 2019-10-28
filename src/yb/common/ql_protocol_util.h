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

#ifndef YB_COMMON_QL_PROTOCOL_UTIL_H
#define YB_COMMON_QL_PROTOCOL_UTIL_H

#include <boost/preprocessor/cat.hpp>
#include <boost/preprocessor/seq/for_each.hpp>

#include "yb/common/ql_protocol.pb.h"
#include "yb/common/jsonb.h"

#include "yb/util/yb_partition.h"

namespace yb {

struct ColumnId;
class QLRowBlock;
class Schema;

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

std::unique_ptr<QLRowBlock> CreateRowBlock(QLClient client, const Schema& schema, Slice data);

// Does this write request require reading existing data for evaluating expressions before writing?
bool RequireReadForExpressions(const QLWriteRequestPB& request);

// Does this write request require reading existing data in general before writing?
bool RequireRead(const QLWriteRequestPB& request, const Schema& schema);

// Does this write request perform a range operation (e.g. range delete)?
bool IsRangeOperation(const QLWriteRequestPB& request, const Schema& schema);

} // namespace yb

#endif // YB_COMMON_QL_PROTOCOL_UTIL_H
