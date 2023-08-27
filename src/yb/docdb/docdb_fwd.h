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

#include "yb/docdb/docdb.fwd.h"
#include "yb/dockv/dockv_fwd.h"

#include "yb/qlexpr/qlexpr_fwd.h"

#include "yb/util/enums.h"
#include "yb/util/math_util.h"
#include "yb/util/strongly_typed_bool.h"

namespace yb::docdb {

class ConsensusFrontier;
class DeadlineInfo;
class DocDBCompactionFilterFactory;
class DocOperation;
class DocPgsqlScanSpec;
class DocQLScanSpec;
class DocRowwiseIterator;
class DocWriteBatch;
class ExternalTxnIntentsState;
class HistoryRetentionPolicy;
class IntentAwareIterator;
class IntentAwareIteratorIf;
class IntentIterator;
class ManualHistoryRetentionPolicy;
class PgsqlWriteOperation;
class QLWriteOperation;
class RedisWriteOperation;
class ScanChoices;
class SchemaPackingProvider;
class SharedLockManager;
class TableInfoProvider;
class TransactionStatusCache;
class WaitQueue;
class YQLRowwiseIteratorIf;
class YQLStorageIf;

struct ApplyTransactionState;
struct DocDB;
struct DocReadContext;
struct FetchedEntry;
struct HistoryRetentionDirective;
struct IntentKeyValueForCDC;
struct KeyBounds;
struct LockBatchEntry;
struct ReadOperationData;

using DocKeyHash = uint16_t;
using LockBatchEntries = std::vector<LockBatchEntry>;
using DocReadContextPtr = std::shared_ptr<DocReadContext>;
using ScanChoicesPtr = std::unique_ptr<ScanChoices>;

using IndexRequests = std::vector<std::pair<const qlexpr::IndexInfo*, QLWriteRequestPB>>;

YB_STRONGLY_TYPED_BOOL(SkipFlush);
YB_STRONGLY_TYPED_BOOL(SkipSeek);

}  // namespace yb::docdb
