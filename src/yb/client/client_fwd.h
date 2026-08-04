//
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
//

#pragma once

#include <functional>
#include <memory>
#include <vector>

#include <boost/function/function_fwd.hpp>

#include "yb/cdc/xrepl_types.h"

#include "yb/common/common_fwd.h"
#include "yb/common/entity_ids_types.h"

#include "yb/util/enums.h"
#include "yb/util/status_fwd.h"
#include "yb/util/strongly_typed_bool.h"

template <class T>
class scoped_refptr;

YB_STRONGLY_TYPED_BOOL(RequireTabletsRunning);
YB_STRONGLY_TYPED_BOOL(PartitionsOnly);

namespace yb::client {

class RejectionScoreSource;
using RejectionScoreSourcePtr = std::shared_ptr<RejectionScoreSource>;

class YBClient;
class YBClientBuilder;

class YBError;
using CollectedErrors = std::vector<std::unique_ptr<YBError>>;

class YBTransaction;
using YBTransactionPtr = std::shared_ptr<YBTransaction>;

class YBqlOp;
class YBqlReadOp;
class YBqlWriteOp;

class YBPgsqlLockOp;
class YBPgsqlOp;
class YBPgsqlReadOp;
class YBPgsqlWriteOp;

class YBRedisOp;
class YBRedisReadOp;
class YBRedisWriteOp;

class YBSession;
using YBSessionPtr = std::shared_ptr<YBSession>;
struct FlushStatus;
using FlushCallback = boost::function<void(FlushStatus*)>;
using CommitCallback = boost::function<void(const Status&)>;
using CreateCallback = boost::function<void(const Status&)>;

class YBTable;
using YBTablePtr = std::shared_ptr<YBTable>;
using TablePartitionList = std::vector<PartitionKey>;
using PartitionListVersion = uint32_t;
struct VersionedTablePartitionList;
struct VersionedPartitionStartKey;

class YBOperation;
using YBOperationPtr = std::shared_ptr<YBOperation>;

class TableHandle;
class TransactionManager;
class TransactionPool;
class UniverseKeyClient;
class YBColumnSpec;
class YBLoggingCallback;
class YBMetaDataCache;
class YBNamespaceAlterer;
class YBSchema;
class YBTableAlterer;
class YBTableCreator;
class YBTableName;

struct ChildTransactionData;
struct TableSizeInfo;
struct TabletServersInfo;
struct VersionedTablePartitionList;
struct YBTableInfo;
struct YBTabletServer;
struct YBTabletServerPlacementInfo;
struct YBqlWriteHashKeyComparator;
struct YBqlWritePrimaryKeyComparator;

using LocalTabletFilter = std::function<void(std::vector<const TabletId*>*)>;
using VersionedTablePartitionListPtr = std::shared_ptr<const VersionedTablePartitionList>;
using TablePartitionListPtr = std::shared_ptr<const TablePartitionList>;
using YBqlOpPtr = std::shared_ptr<YBqlOp>;
using YBqlReadOpPtr = std::shared_ptr<YBqlReadOp>;
using YBqlWriteOpPtr = std::shared_ptr<YBqlWriteOp>;
using YBPgsqlReadOpPtr = std::shared_ptr<YBPgsqlReadOp>;
using YBPgsqlWriteOpPtr = std::shared_ptr<YBPgsqlWriteOp>;
using YBPgsqlLockOpPtr = std::shared_ptr<YBPgsqlLockOp>;

enum class YBTableType;

YB_DEFINE_ENUM(GrantRevokeStatementType, (GRANT)(REVOKE));
YB_STRONGLY_TYPED_BOOL(ForceConsistentRead);
YB_STRONGLY_TYPED_BOOL(Initial);
YB_STRONGLY_TYPED_BOOL(UseCache);
YB_STRONGLY_TYPED_BOOL(ForceCreateTransaction);

namespace internal {

class AsyncRpc;
class GetColocatedTabletSchemaRpc;
class GetTableSchemaRpc;
class GetTablegroupSchemaRpc;
class LookupRpc;
class MetaCache;
class PermissionsCache;
class ReadRpc;
class TabletInvoker;
class TxnBatcherIf;
class WriteRpc;

struct InFlightOp;
struct InFlightOpsGroupsWithMetadata;

class RemoteTablet;
using RemoteTabletPtr = scoped_refptr<RemoteTablet>;

class RemoteTabletServer;
using RemoteTabletServerPtr = std::shared_ptr<RemoteTabletServer>;

class Batcher;
using BatcherPtr = std::shared_ptr<Batcher>;

struct AsyncRpcMetrics;
using AsyncRpcMetricsPtr = std::shared_ptr<AsyncRpcMetrics>;

YB_STRONGLY_TYPED_BOOL(IsWithinTransactionRetry);

} // namespace internal

using LookupTabletCallback = std::function<void(const Result<internal::RemoteTabletPtr>&)>;
using LookupTabletRangeCallback =
    std::function<void(const Result<std::vector<internal::RemoteTabletPtr>>&)>;
using CreateCDCStreamCallback = std::function<void(const Result<xrepl::StreamId>&)>;
using BootstrapProducerResult =
    Result<std::tuple<std::vector<TableId>, std::vector<std::string>, HybridTime>>;
using BootstrapProducerCallback = std::function<void(BootstrapProducerResult)>;

class AsyncClientInitializer;

} // namespace yb::client
