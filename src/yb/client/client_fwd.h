//
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
//

#pragma once

#include <functional>
#include <memory>
#include <set>
#include <utility>
#include <vector>

#include <boost/function/function_fwd.hpp>

#include "yb/cdc/cdc_types.h"

#include "yb/common/common_fwd.h"
#include "yb/common/entity_ids_types.h"

#include "yb/util/status_fwd.h"
#include "yb/util/enums.h"
#include "yb/util/math_util.h"
#include "yb/util/strongly_typed_bool.h"

template <class T>
class scoped_refptr;

YB_STRONGLY_TYPED_BOOL(RequireTabletsRunning);
YB_STRONGLY_TYPED_BOOL(PartitionsOnly);

namespace yb {
namespace client {

class RejectionScoreSource;
typedef std::shared_ptr<RejectionScoreSource> RejectionScoreSourcePtr;

class YBClient;
class YBClientBuilder;

class YBError;
typedef std::vector<std::unique_ptr<YBError>> CollectedErrors;

class YBTransaction;
typedef std::shared_ptr<YBTransaction> YBTransactionPtr;

class YBqlOp;
class YBqlReadOp;
class YBqlWriteOp;

class YBPgsqlOp;
class YBPgsqlReadOp;
class YBPgsqlWriteOp;

class YBRedisOp;
class YBRedisReadOp;
class YBRedisWriteOp;

class YBSession;
typedef std::shared_ptr<YBSession> YBSessionPtr;
struct FlushStatus;
using FlushCallback = boost::function<void(FlushStatus*)>;
using CommitCallback = boost::function<void(const Status&)>;
using CreateCallback = boost::function<void(const Status&)>;

class YBTable;
typedef std::shared_ptr<YBTable> YBTablePtr;
typedef std::vector<PartitionKey> TablePartitionList;
typedef uint32_t PartitionListVersion;
struct VersionedTablePartitionList;
struct VersionedPartitionStartKey;

class YBOperation;
typedef std::shared_ptr<YBOperation> YBOperationPtr;

class TableHandle;
class TransactionManager;
class TransactionPool;
class YBColumnSpec;
class YBLoggingCallback;
class YBMetaDataCache;
class YBNamespaceAlterer;
class YBSchema;
class YBTableAlterer;
class YBTableCreator;
class YBTableName;
class UniverseKeyClient;

struct ChildTransactionData;
struct TableSizeInfo;
struct VersionedTablePartitionList;
struct YBTableInfo;
struct YBTabletServer;
struct YBTabletServerPlacementInfo;
struct YBqlWriteHashKeyComparator;
struct YBqlWritePrimaryKeyComparator;

using LocalTabletFilter = std::function<void(std::vector<const TabletId*>*)>;
using VersionedTablePartitionListPtr = std::shared_ptr<const VersionedTablePartitionList>;
using TablePartitionListPtr = std::shared_ptr<const TablePartitionList>;
using TabletServersInfo = std::vector<YBTabletServerPlacementInfo>;
using YBqlOpPtr = std::shared_ptr<YBqlOp>;
using YBqlReadOpPtr = std::shared_ptr<YBqlReadOp>;
using YBqlWriteOpPtr = std::shared_ptr<YBqlWriteOp>;
using YBPgsqlReadOpPtr = std::shared_ptr<YBPgsqlReadOp>;
using YBPgsqlWriteOpPtr = std::shared_ptr<YBPgsqlWriteOp>;

enum class YBTableType;

YB_DEFINE_ENUM(GrantRevokeStatementType, (GRANT)(REVOKE));
YB_STRONGLY_TYPED_BOOL(ForceConsistentRead);
YB_STRONGLY_TYPED_BOOL(ForceGlobalTransaction);
YB_STRONGLY_TYPED_BOOL(Initial);
YB_STRONGLY_TYPED_BOOL(UseCache);

namespace internal {

class AsyncRpc;
class TxnBatcherIf;
class GetTableSchemaRpc;
class GetTablegroupSchemaRpc;
class GetColocatedTabletSchemaRpc;
class LookupRpc;
class MetaCache;
class PermissionsCache;
class ReadRpc;
class TabletInvoker;
class WriteRpc;

struct InFlightOp;
struct InFlightOpsGroupsWithMetadata;

class RemoteTablet;
typedef scoped_refptr<RemoteTablet> RemoteTabletPtr;

class RemoteTabletServer;

class Batcher;
using BatcherPtr = std::shared_ptr<Batcher>;

struct AsyncRpcMetrics;
typedef std::shared_ptr<AsyncRpcMetrics> AsyncRpcMetricsPtr;

YB_STRONGLY_TYPED_BOOL(IsWithinTransactionRetry);

} // namespace internal

typedef std::function<void(const Result<internal::RemoteTabletPtr>&)> LookupTabletCallback;
typedef std::function<void(const Result<std::vector<internal::RemoteTabletPtr>>&)>
        LookupTabletRangeCallback;
typedef std::function<void(const Result<xrepl::StreamId>&)> CreateCDCStreamCallback;
typedef Result<std::tuple<
    std::vector<TableId> /* table_ids */, std::vector<std::string> /* bootstrap_ids */,
    HybridTime /* bootstrap_time */>>
    BootstrapProducerResult;
typedef std::function<void(BootstrapProducerResult)> BootstrapProducerCallback;
class AsyncClientInitializer;

} // namespace client
} // namespace yb
