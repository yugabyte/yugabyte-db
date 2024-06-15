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

#include <memory>
#include <vector>

#include "yb/common/common.fwd.h"
#include "yb/common/pgsql_protocol.fwd.h"
#include "yb/common/ql_protocol.fwd.h"
#include "yb/common/redis_protocol.fwd.h"
#include "yb/common/wire_protocol.fwd.h"
#include "yb/util/strongly_typed_bool.h"

namespace yb {

class ClockBase;
class ColumnId;
class ColumnSchema;
class ConstContiguousRow;
class DocHybridTime;
class EncodedDocHybridTime;
class HybridTime;
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

namespace common {

class Jsonb;

} // namespace common

} // namespace yb
