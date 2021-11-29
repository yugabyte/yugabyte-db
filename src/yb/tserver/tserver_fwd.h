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

#ifndef YB_TSERVER_TSERVER_FWD_H
#define YB_TSERVER_TSERVER_FWD_H

namespace yb {
namespace tserver {

class AbortTransactionRequestPB;
class AbortTransactionResponsePB;
class GetTabletStatusRequestPB;
class GetTabletStatusResponsePB;
class GetTransactionStatusAtParticipantRequestPB;
class GetTransactionStatusAtParticipantResponsePB;
class GetTransactionStatusRequestPB;
class GetTransactionStatusResponsePB;
class Heartbeater;
class LocalTabletServer;
class MetricsSnapshotter;
class PgCreateColumnPB;
class PgCreateTableRequestPB;
class TSTabletManager;
class TabletPeerLookupIf;
class TabletServer;
class TabletServerAdminServiceProxy;
class TabletServerBackupServiceProxy;
class TabletServerIf;
class TabletServerOptions;
class TabletServerServiceProxy;
class TabletServerForwardServiceProxy;
class TabletServiceImpl;
class TabletServerPathHandlers;
class TabletSnapshotOpRequestPB;
class TabletSnapshotOpResponsePB;
class TransactionStatePB;
class UpdateTransactionRequestPB;
class UpdateTransactionResponsePB;

enum class TabletServerServiceRpcMethodIndexes;

} // namespace tserver
} // namespace yb

#endif // YB_TSERVER_TSERVER_FWD_H
