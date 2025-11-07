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

#include "yb/common/common_fwd.h"

#include "yb/util/enums.h"
#include "yb/util/math_util.h"
#include "yb/util/strongly_typed_bool.h"

namespace yb::dockv {

class DocKey;
class DocKeyDecoder;
class DocPath;
class DocVectorValue;
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
class SchemaPackingRegistry;
class SchemaPackingStorage;
class SubDocKey;
class YBPartialRow;

struct ColumnPackingData;
struct ProjectedColumn;
struct ReaderProjection;
struct ValueControlFields;

using DocKeyHash = uint16_t;
using KeyEntryValues = std::vector<KeyEntryValue>;
using FrozenContainer = KeyEntryValues;

enum class KeyEntryType;
enum class ValueEntryType;
enum class YBHashSchema;

// Automatically decode keys that are stored in string-typed PrimitiveValues when converting a
// PrimitiveValue to string. This is useful when displaying write batches for secondary indexes.
YB_STRONGLY_TYPED_BOOL(AutoDecodeKeys);
YB_STRONGLY_TYPED_BOOL(PartialRangeKeyIntents);
YB_STRONGLY_TYPED_BOOL(UseHash);

// Indicates whether skip prefix locks is enabled.
YB_STRONGLY_TYPED_BOOL(SkipPrefixLocks);

YB_DEFINE_ENUM(OperationKind, (kRead)(kWrite));

// "Intent types" are used for single-tablet operations and cross-shard transactions. For example,
// multiple write-only operations don't need to conflict. However, if one operation is a
// read-modify-write snapshot isolation operation, then a write-only operation cannot proceed in
// parallel with it. Conflicts between intent types are handled according to the conflict matrix at
// https://goo.gl/Wbc663.

// "Weak" intents are obtained for prefix SubDocKeys of a key that a transaction is working with.
// E.g. if we're writing "a.b.c", we'll obtain weak write intents on "a" and "a.b", but a strong
// write intent on "a.b.c".
constexpr int kWeakIntentFlag         = 0b000;

// "Strong" intents are obtained on the fully qualified SubDocKey that an operation is working with.
// See the example above.
constexpr int kStrongIntentFlag       = 0b010;

constexpr int kReadIntentFlag         = 0b000;
constexpr int kWriteIntentFlag        = 0b001;

// We put weak intents before strong intents to be able to skip weak intents while checking for
// conflicts.
//
// This was not always the case.
// kObsoleteIntentTypeSet corresponds to intent type set values stored in such a way that
// strong/weak and read/write bits are swapped compared to the current format.
YB_DEFINE_ENUM(IntentType,
    ((kWeakRead,      kWeakIntentFlag |  kReadIntentFlag))
    ((kWeakWrite,     kWeakIntentFlag | kWriteIntentFlag))
    ((kStrongRead,  kStrongIntentFlag |  kReadIntentFlag))
    ((kStrongWrite, kStrongIntentFlag | kWriteIntentFlag))
);

using IntentTypeSet = EnumBitSet<IntentType>;

}  // namespace yb::dockv
