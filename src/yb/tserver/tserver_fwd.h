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

#include <functional>
#include <type_traits>
#include <utility>

#include "yb/tserver/backup.fwd.h"
#include "yb/tserver/pg_client.fwd.h"
#include "yb/tserver/tserver.fwd.h"
#include "yb/tserver/tserver_service.fwd.h"

#include "yb/util/strongly_typed_bool.h"
#include "yb/util/strongly_typed_uuid.h"

namespace yb {

namespace client {

class TransactionManager;
class TransactionPool;
class YBPgsqlOp;

}

namespace tserver {

class DbServerBase;
class Heartbeater;
class LocalTabletServer;
class MetricsSnapshotter;
class PgClientServiceMockImpl;
class PgMutationCounter;
class PgTableCache;
class PgTablesQueryResult;
class PgResponseCache;
class PgSequenceCache;
class PgSharedMemoryPool;
class SharedExchange;
class SharedMemoryManager;
class SharedMemorySegmentHandle;
class TSLocalLockManager;
class TSTabletManager;
class TableMutationCountSender;
class TabletPeerLookupIf;
class TabletServer;
class TabletServerAdminServiceProxy;
class TabletServerBackupServiceProxy;
class TabletServerIf;
class TabletServerOptions;
class TabletServerServiceProxy;
class TabletServiceImpl;
class TabletServerPathHandlers;
class TserverXClusterContextIf;
class YSQLLeaseManager;
class YsqlAdvisoryLocksTable;

enum class TabletServerServiceRpcMethodIndexes;

YB_STRONGLY_TYPED_BOOL(AllowSplitTablet);

using TSLocalLockManagerPtr = std::shared_ptr<TSLocalLockManager>;
using TransactionManagerProvider = std::function<client::TransactionManager&()>;
using TransactionPoolProvider = std::function<client::TransactionPool&()>;

template <typename, typename = std::void_t<>>
struct HasTabletConsensusInfo : std::false_type {};

template <typename T>
struct HasTabletConsensusInfo<
    T, std::void_t<decltype(std::declval<T>().tablet_consensus_info())>>
    : std::true_type {};

template <typename, typename = std::void_t<>>
struct HasRaftConfigOpidIndex : std::false_type {};

template <typename T>
struct HasRaftConfigOpidIndex<
    T, std::void_t<decltype(std::declval<T>().raft_config_opid_index())>>
    : std::true_type {};

struct PgTxnSnapshot;
YB_STRONGLY_TYPED_UUID_DECL(PgTxnSnapshotLocalId);

// Temporary typedef for lightweight protobuf migration
using TabletConsensusInfoMsg = TabletConsensusInfoPB;
using ReadRequestMsg = ReadRequestPB;
using ReadResponseMsg = ReadResponsePB;
using WriteRequestMsg = WriteRequestPB;
using WriteResponseMsg = WriteResponsePB;
using PgPerformRequestMsg = PgPerformRequestPB;
using PgPerformResponseMsg = PgPerformResponsePB;
using PgAcquireObjectLockRequestMsg = PgAcquireObjectLockRequestPB;
using PgAcquireObjectLockResponseMsg = PgAcquireObjectLockResponsePB;
using PgInsertSequenceTupleRequestMsg = PgInsertSequenceTupleRequestPB;
using PgInsertSequenceTupleResponseMsg = PgInsertSequenceTupleResponsePB;
using PgUpdateSequenceTupleRequestMsg = PgUpdateSequenceTupleRequestPB;
using PgUpdateSequenceTupleResponseMsg = PgUpdateSequenceTupleResponsePB;
using PgFetchSequenceTupleRequestMsg = PgFetchSequenceTupleRequestPB;
using PgFetchSequenceTupleResponseMsg = PgFetchSequenceTupleResponsePB;
using PgReadSequenceTupleRequestMsg = PgReadSequenceTupleRequestPB;
using PgReadSequenceTupleResponseMsg = PgReadSequenceTupleResponsePB;
using PgDeleteSequenceTupleRequestMsg = PgDeleteSequenceTupleRequestPB;
using PgDeleteSequenceTupleResponseMsg = PgDeleteSequenceTupleResponsePB;
using PgDeleteDBSequencesRequestMsg = PgDeleteDBSequencesRequestPB;
using PgDeleteDBSequencesResponseMsg = PgDeleteDBSequencesResponsePB;
using PgGetTableKeyRangesRequestMsg = PgGetTableKeyRangesRequestPB;
using PgGetTableKeyRangesResponseMsg = PgGetTableKeyRangesResponsePB;
using TabletServerErrorMsg = TabletServerErrorPB;

} // namespace tserver
} // namespace yb
