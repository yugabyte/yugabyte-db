// Copyright (c) YugabyteDB, Inc.
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
#include <vector>

#include "yb/common/common.fwd.h"
#include "yb/common/pgsql_protocol.fwd.h"
#include "yb/common/ql_protocol.fwd.h"
#include "yb/common/redis_protocol.fwd.h"
#include "yb/common/wire_protocol.fwd.h"
#include "yb/util/strongly_typed_bool.h"

namespace google::protobuf {
template <typename Element> class RepeatedPtrField;
}

namespace yb {

class ClockBase;
class ColumnId;
class ColumnSchema;
class ConstContiguousRow;
class DocHybridTime;
class EncodedDocHybridTime;
class HybridTime;
struct ReadRestartData;
class MissingValueProvider;
class TableProperties;
class TransactionStatusManager;
class TypeInfo;

class Schema;
typedef std::shared_ptr<Schema> SchemaPtr;

typedef std::string PartitionKey;
typedef std::shared_ptr<const PartitionKey> PartitionKeyPtr;

class QLType;
class QLValue;

struct DeletedColumn;
struct OpId;
struct PgObjectId;
struct ReadHybridTime;
struct TransactionFullLocality;
struct TransactionMetadata;
struct TransactionOperationContext;
struct TransactionStatusResult;

using ColocationId = uint32_t;
using SchemaVersion = uint32_t;

using QLTypePtr = std::shared_ptr<QLType>;

using PgObjectIds = std::vector<PgObjectId>;

enum class PgSystemAttrNum : int;
enum class DataType;

enum class SortingType;

YB_STRONGLY_TYPED_BOOL(ClampUncertaintyWindow);

// Temporary typedef for lightweight protobuf migration
using QLRSColDescMsg = QLRSColDescPB;
using QLRSRowDescMsg = QLRSRowDescPB;
using QLWriteRequestMsg = QLWriteRequestPB;
using QLValueMsg = QLValuePB;
using QLExpressionMsg = QLExpressionPB;
using PgsqlExpressionMsg = PgsqlExpressionPB;
using RedisResponseMsg = RedisResponsePB;
using RedisWriteRequestMsg = RedisWriteRequestPB;
using RedisReadRequestMsg = RedisReadRequestPB;
using QLResponseMsg = QLResponsePB;
using QLReadRequestMsg = QLReadRequestPB;
using PgsqlResponseMsg = PgsqlResponsePB;
using PgsqlWriteRequestMsg = PgsqlWriteRequestPB;
using PgsqlReadRequestMsg = PgsqlReadRequestPB;
using PgsqlLockRequestMsg = PgsqlLockRequestPB;
using QLTypeMsg = QLTypePB;
using ChildTransactionDataMsg = ChildTransactionDataPB;
using ChildTransactionResultMsg = ChildTransactionResultPB;
using QLJsonOperationMsg = QLJsonOperationPB;
using QLReferencedColumnsMsg = QLReferencedColumnsPB;
using QLConditionMsg = QLConditionPB;
using QLColumnValueMsg = QLColumnValuePB;
using PgsqlColumnValueMsg = PgsqlColumnValuePB;
using PgVectorReadOptionsMsg = PgVectorReadOptionsPB;
using RedisKeyValueMsg = RedisKeyValuePB;
using TransactionMetadataMsg = TransactionMetadataPB;
using SubTransactionMetadataMsg = SubTransactionMetadataPB;
using RedisGetRequestMsg = RedisGetRequestPB;
using RedisSubKeyBoundMsg = RedisSubKeyBoundPB;
using RedisKeyValueSubKeyMsg = RedisKeyValueSubKeyPB;
using RedisArrayMsg = RedisArrayPB;
using QLMapValueMsg = QLMapValuePB;
using PgsqlColRefMsg = PgsqlColRefPB;
using PgsqlBatchArgumentMsg = PgsqlBatchArgumentPB;
using PgsqlSamplingStateMsg = PgsqlSamplingStatePB;
using PgsqlSampleBlockMsg = PgsqlSampleBlockPB;
using SortedSetOptionsMsg = SortedSetOptionsPB;
using RedisIndexBoundMsg = RedisIndexBoundPB;
using RedisCollectionGetRangeRequestMsg = RedisCollectionGetRangeRequestPB;
using PgsqlPagingStateMsg = PgsqlPagingStatePB;
using PgsqlAdvisoryLockMsg = PgsqlAdvisoryLockPB;

using QLReadRequestMsgs = google::protobuf::RepeatedPtrField<QLReadRequestMsg>;
using PgsqlBatchArgumentMsgs = google::protobuf::RepeatedPtrField<PgsqlBatchArgumentMsg>;
using PgsqlExpressionMsgs = google::protobuf::RepeatedPtrField<PgsqlExpressionMsg>;
using PgsqlReadRequestMsgs = google::protobuf::RepeatedPtrField<PgsqlReadRequestMsg>;
using QLExpressionMsgs = google::protobuf::RepeatedPtrField<QLExpressionMsg>;

namespace common {

class Jsonb;

} // namespace common

} // namespace yb
