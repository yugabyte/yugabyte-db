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

#include <functional>
#include <type_traits>
#include <utility>

#include "yb/tserver/backup.fwd.h"
#include "yb/tserver/tserver.fwd.h"
#include "yb/tserver/tserver_service.fwd.h"

#include "yb/util/strongly_typed_bool.h"
#include "yb/util/strongly_typed_uuid.h"

namespace yb {

namespace client {

class TransactionPool;
class YBPgsqlOp;

}

namespace tserver {

class Heartbeater;
class LocalTabletServer;
class MetricsSnapshotter;
class PgClientServiceMockImpl;
class PgTableCache;
class PgResponseCache;
class PgSequenceCache;
class PgSharedMemoryPool;
class SharedExchange;
class SharedMemorySegmentHandle;
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

enum class TabletServerServiceRpcMethodIndexes;

YB_STRONGLY_TYPED_BOOL(AllowSplitTablet);

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

} // namespace tserver
} // namespace yb
