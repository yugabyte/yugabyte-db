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
#include "yb/util/memory/arena_fwd.h"
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
using QLRSColDescMsg = LWQLRSColDescPB;
using QLRSRowDescMsg = LWQLRSRowDescPB;
using QLWriteRequestMsg = LWQLWriteRequestPB;
using QLValueMsg = LWQLValuePB;
using QLExpressionMsg = LWQLExpressionPB;
using PgsqlExpressionMsg = LWPgsqlExpressionPB;
using RedisResponseMsg = LWRedisResponsePB;
using RedisWriteRequestMsg = LWRedisWriteRequestPB;
using RedisReadRequestMsg = LWRedisReadRequestPB;
using QLResponseMsg = LWQLResponsePB;
using QLReadRequestMsg = LWQLReadRequestPB;
using PgsqlResponseMsg = LWPgsqlResponsePB;
using PgsqlWriteRequestMsg = LWPgsqlWriteRequestPB;
using PgsqlReadRequestMsg = LWPgsqlReadRequestPB;
using PgsqlLockRequestMsg = LWPgsqlLockRequestPB;
using QLTypeMsg = LWQLTypePB;
using ChildTransactionDataMsg = LWChildTransactionDataPB;
using ChildTransactionResultMsg = LWChildTransactionResultPB;
using QLJsonOperationMsg = LWQLJsonOperationPB;
using QLReferencedColumnsMsg = LWQLReferencedColumnsPB;
using QLConditionMsg = LWQLConditionPB;
using QLColumnValueMsg = LWQLColumnValuePB;
using PgsqlColumnValueMsg = LWPgsqlColumnValuePB;
using PgVectorReadOptionsMsg = LWPgVectorReadOptionsPB;
using RedisKeyValueMsg = LWRedisKeyValuePB;
using TransactionMetadataMsg = LWTransactionMetadataPB;
using SubTransactionMetadataMsg = LWSubTransactionMetadataPB;
using RedisGetRequestMsg = LWRedisGetRequestPB;
using RedisSubKeyBoundMsg = LWRedisSubKeyBoundPB;
using RedisKeyValueSubKeyMsg = LWRedisKeyValueSubKeyPB;
using RedisArrayMsg = LWRedisArrayPB;
using QLMapValueMsg = LWQLMapValuePB;
using PgsqlColRefMsg = LWPgsqlColRefPB;
using PgsqlBatchArgumentMsg = LWPgsqlBatchArgumentPB;
using PgsqlSamplingStateMsg = LWPgsqlSamplingStatePB;
using PgsqlSampleBlockMsg = LWPgsqlSampleBlockPB;
using SortedSetOptionsMsg = LWSortedSetOptionsPB;
using RedisIndexBoundMsg = LWRedisIndexBoundPB;
using RedisCollectionGetRangeRequestMsg = LWRedisCollectionGetRangeRequestPB;
using PgsqlPagingStateMsg = LWPgsqlPagingStatePB;
using PgsqlAdvisoryLockMsg = LWPgsqlAdvisoryLockPB;

using QLReadRequestMsgs = ArenaList<QLReadRequestMsg>;
using PgsqlBatchArgumentMsgs = ArenaList<PgsqlBatchArgumentMsg>;
using PgsqlExpressionMsgs = ArenaList<PgsqlExpressionMsg>;
using PgsqlReadRequestMsgs = ArenaList<PgsqlReadRequestMsg>;
using QLExpressionMsgs = ArenaList<QLExpressionMsg>;

namespace common {

class Jsonb;

} // namespace common

} // namespace yb
