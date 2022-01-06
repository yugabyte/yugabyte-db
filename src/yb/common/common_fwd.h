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

#include "yb/common/common.fwd.h"
#include "yb/common/pgsql_protocol.fwd.h"
#include "yb/common/ql_protocol.fwd.h"
#include "yb/common/redis_protocol.fwd.h"
#include "yb/common/wire_protocol.fwd.h"

namespace yb {

class ClockBase;
class ColumnId;
class ColumnSchema;
class DocHybridTime;
class HybridTime;
class IndexInfo;
class IndexMap;
class Partition;
class PartitionSchema;
class PgsqlScanSpec;
class QLRow;
class QLRowBlock;
class QLScanSpec;
class TableProperties;
class TransactionStatusManager;
class TypeInfo;

class Schema;
typedef std::shared_ptr<Schema> SchemaPtr;

typedef std::string PartitionKey;
typedef std::shared_ptr<const PartitionKey> PartitionKeyPtr;

class QLExprExecutor;
typedef std::shared_ptr<QLExprExecutor> QLExprExecutorPtr;

class QLTableRow;
class QLType;
class QLValue;

struct DeletedColumn;
struct IndexColumn;
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
enum class YBHashSchema;

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
