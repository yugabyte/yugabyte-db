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

#include "yb/common/common_fwd.h"

#include "yb/dockv/value_type.h"

#include "yb/util/enums.h"
#include "yb/util/math_util.h"
#include "yb/util/strongly_typed_bool.h"

namespace yb::dockv {

class DocKey;
class DocKeyDecoder;
class DocPath;
class KeyBytes;
class KeyEntryValue;
class PackedValueV1;
class PackedValueV2;
class PackedRowDecoderBase;
class PackedRowDecoderV1;
class PackedRowDecoderV2;
class Partition;
class PartitionSchema;
class PgKeyDecoder;
class PgTableRow;
class PgValue;
class PrimitiveValue;
class RowPackerV1;
class RowPackerV2;
class SchemaPacking;
class SchemaPackingStorage;
class SubDocKey;
class YBPartialRow;

struct ColumnPackingData;
struct ProjectedColumn;
struct ReaderProjection;
struct ValueControlFields;

using DocKeyHash = uint16_t;
using KeyEntryValues = std::vector<KeyEntryValue>;

enum class KeyEntryType;
enum class ValueEntryType;
enum class YBHashSchema;

// Automatically decode keys that are stored in string-typed PrimitiveValues when converting a
// PrimitiveValue to string. This is useful when displaying write batches for secondary indexes.
YB_STRONGLY_TYPED_BOOL(AutoDecodeKeys);
YB_STRONGLY_TYPED_BOOL(PartialRangeKeyIntents);
YB_STRONGLY_TYPED_BOOL(UseHash);

YB_DEFINE_ENUM(OperationKind, (kRead)(kWrite));

}  // namespace yb::dockv
