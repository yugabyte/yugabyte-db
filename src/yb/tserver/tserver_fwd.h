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

#include "yb/tserver/backup.fwd.h"
#include "yb/tserver/tserver.fwd.h"
#include "yb/tserver/tserver_service.fwd.h"

#include "yb/util/strongly_typed_bool.h"

namespace yb {

namespace client {

class TransactionPool;
class YBPgsqlOp;

}

namespace tserver {

class Heartbeater;
class LocalTabletServer;
class MetricsSnapshotter;
class PgTableCache;
class PgResponseCache;
class PgSequenceCache;
class SharedExchange;
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

} // namespace tserver
} // namespace yb
