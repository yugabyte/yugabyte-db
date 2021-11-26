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

#ifndef YB_COMMON_COMMON_FWD_H
#define YB_COMMON_COMMON_FWD_H

#include <memory>
#include <unordered_set>

namespace yb {

class ClockBase;
class CloudInfoPB;
class ColumnId;
class ColumnSchema;
class DocHybridTime;
class HostPortPB;
class HybridTime;
class IndexInfo;
class IndexMap;
class NodeInstancePB;
class Partition;
class PartitionSchema;
class PgsqlScanSpec;
class PgsqlResponsePB;
class PgsqlRSColDescPB;
class QLRow;
class QLRowBlock;
class QLScanSpec;
class ServerEntryPB;
class ServerRegistrationPB;
class TableProperties;
class TransactionMetadataPB;
class TransactionStatusManager;
class TypeInfo;
class YQLRowwiseIteratorIf;
class YQLStorageIf;

class Schema;
typedef std::shared_ptr<Schema> SchemaPtr;

typedef std::string PartitionKey;
typedef std::shared_ptr<const PartitionKey> PartitionKeyPtr;

class PgsqlBCallPB;
class PgsqlConditionPB;
class PgsqlExpressionPB;
class PgsqlReadRequestPB;
class PgsqlRSRowDescPB;
class PgsqlWriteRequestPB;

class QLExprExecutor;
typedef std::shared_ptr<QLExprExecutor> QLExprExecutorPtr;

class QLJsonColumnOperationsPB;
class QLJsonOperationPB;
class QLPagingStatePB;
class QLReadRequestPB;
class QLRSColDescPB;
class QLRSRowDescPB;
class QLTableRow;
class QLType;
class QLValue;
class QLValuePB;

class RedisReadRequestPB;
class RedisResponsePB;

struct DeletedColumn;
struct OpId;
struct QLTableColumn;
struct ReadHybridTime;
struct TransactionMetadata;
struct TransactionOperationContext;
struct TransactionStatusResult;

using PgTableOid = uint32_t;
using QLTypePtr = std::shared_ptr<QLType>;

enum class PgSystemAttrNum : int;
enum class QLNameOption : int8_t;

enum SortingType : uint8_t {
  kNotSpecified = 0,
  kAscending,          // ASC, NULLS FIRST
  kDescending,         // DESC, NULLS FIRST
  kAscendingNullsLast, // ASC, NULLS LAST
  kDescendingNullsLast // DESC, NULLS LAST
};

namespace common {

class Jsonb;

} // namespace common

} // namespace yb

#endif // YB_COMMON_COMMON_FWD_H
