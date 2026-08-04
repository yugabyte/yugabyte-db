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

#include "yb/docdb/docdb.fwd.h"
#include "yb/docdb/object_lock_shared_fwd.h"
#include "yb/dockv/dockv_fwd.h"

#include "yb/qlexpr/qlexpr_fwd.h"

#include "yb/util/clone_ptr.h"
#include "yb/util/enums.h"
#include "yb/util/math_util.h"
#include "yb/util/ref_cnt_buffer.h"
#include "yb/util/strongly_typed_bool.h"

namespace yb::docdb {

class BloomFilterOptions;
class BoundedRocksDbIterator;
class ConsensusFrontier;
class DeadlineInfo;
class DocDBCompactionFilterFactory;
class DocDBStatistics;
class DocOperation;
class DocPgsqlScanSpec;
class DocQLScanSpec;
class DocRowwiseIterator;
class DocVectorIndex;
class DocWriteBatch;
class HistoryRetentionPolicy;
class IntentAwareIterator;
class IntentAwareIteratorBoundsScope;
class LocalWaitingTxnRegistry;
class LockBatch;
class ManualHistoryRetentionPolicy;
class ObjectLockManager;
class PgsqlWriteOperation;
class QLWriteOperation;
class RedisWriteOperation;
class ScanChoices;
class SchemaPackingProvider;
class SharedLockManager;
class StorageSet;
class TableInfoProvider;
class TransactionStatusCache;
class WaitQueue;
class YQLRowwiseIteratorIf;
class YQLStorageIf;

struct ApplyTransactionState;
struct DocDB;
struct DocReadContext;
struct DocVectorIndexInsertEntry;
struct DocVectorIndexSearchResult;
struct DocVectorIndexSearchResultEntry;
struct FetchedEntry;
struct HistoryRetentionDirective;
struct IntentKeyValueForCDC;
struct KeyBounds;
template <typename T>
struct LockBatchEntry;
struct ObjectLockOwner;
struct ObjectLockPrefix;
struct PgsqlReadOperationData;
struct ReadOperationData;

using DocKeyHash = uint16_t;
using DocReadContextPtr = std::shared_ptr<const DocReadContext>;
using DocRowwiseIteratorPtr = std::unique_ptr<DocRowwiseIterator>;
using IntentAwareIteratorPtr = std::unique_ptr<IntentAwareIterator>;
using IntentAwareIteratorBoundsScopePtr = std::unique_ptr<IntentAwareIteratorBoundsScope>;
// The bounds scope restores the iterator's previous bounds from its destructor, so it must be torn
// down while the iterator is still alive. This cannot be a std::tuple: the order in which a tuple
// destroys its elements is unspecified, and libstdc++ and libc++ pick opposite directions, so a
// tuple is only correct for one of them. Class members are destroyed in reverse declaration order
// on every implementation, so `bounds` must stay declared after `iter`.
struct IntentAwareIteratorWithBounds {
  IntentAwareIteratorPtr iter;
  IntentAwareIteratorBoundsScopePtr bounds;
};

template <typename LockManager>
using LockBatchEntries = std::vector<LockBatchEntry<LockManager>>;
// Lock state stores the number of locks acquired for each intent type.
// The count for each intent type resides in sequential bits (block) in lock state.
// For example the count of locks on a particular intent type could be received as:
// (lock_state >> (std::to_underlying(intent_type) * kIntentTypeBits)) & kFirstIntentTypeMask.
// Refer shared_lock_manager.cc for further details.
using LockState = uint64_t;
using ScanChoicesPtr = std::unique_ptr<ScanChoices>;

using ConsensusFrontierPtr = clone_ptr<ConsensusFrontier>;
using IndexRequests = std::vector<std::pair<const qlexpr::IndexInfo*, QLWriteRequestPB>>;
using DocVectorIndexPtr = std::shared_ptr<DocVectorIndex>;
using DocVectorIndexes = std::vector<DocVectorIndexPtr>;
using DocVectorIndexesPtr = std::shared_ptr<DocVectorIndexes>;
using DocVectorIndexInsertEntries = std::vector<DocVectorIndexInsertEntry>;

YB_STRONGLY_TYPED_BOOL(AvoidUselessNextInsteadOfSeek);
YB_STRONGLY_TYPED_BOOL(AllowVariableBloomFilter);
YB_STRONGLY_TYPED_BOOL(FastBackwardScan);
YB_STRONGLY_TYPED_BOOL(IncludeIntents);
YB_STRONGLY_TYPED_BOOL(SkipFlush);
YB_STRONGLY_TYPED_BOOL(SkipSeek);
YB_STRONGLY_TYPED_BOOL(UpdateFilterKey);

// Flags that alter IntentAwareIterator behavior relative to the defaults. kNoFastNext keeps all
// reads on the iterator's creation-time snapshot: fast next skips sequence number filtering, so it
// can observe records written to the regular DB after the iterator was created.
YB_DEFINE_ENUM(IntentAwareIteratorFlag,
               (kFastBackwardScan)(kAvoidUselessNextInsteadOfSeek)(kNoFastNext));
using IntentAwareIteratorFlags = EnumBitSet<IntentAwareIteratorFlag>;

using dockv::IncludeWriteTime;

}  // namespace yb::docdb
