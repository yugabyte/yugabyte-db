// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.

#include "kudu/client/client-internal.h"

#include <algorithm>
#include <limits>
#include <string>
#include <vector>

#include "kudu/client/meta_cache.h"
#include "kudu/common/schema.h"
#include "kudu/common/wire_protocol.h"
#include "kudu/gutil/map-util.h"
#include "kudu/gutil/strings/human_readable.h"
#include "kudu/gutil/strings/join.h"
#include "kudu/gutil/strings/substitute.h"
#include "kudu/gutil/sysinfo.h"
#include "kudu/master/master.h"
#include "kudu/master/master_rpc.h"
#include "kudu/master/master.pb.h"
#include "kudu/master/master.proxy.h"
#include "kudu/rpc/rpc.h"
#include "kudu/rpc/rpc_controller.h"
#include "kudu/util/net/dns_resolver.h"
#include "kudu/util/net/net_util.h"
#include "kudu/util/thread_restrictions.h"

using std::set;
using std::shared_ptr;
using std::string;
using std::vector;

namespace kudu {

using consensus::RaftPeerPB;
using master::AlterTableRequestPB;
using master::AlterTableResponsePB;
using master::CreateTableRequestPB;
using master::CreateTableResponsePB;
using master::DeleteTableRequestPB;
using master::DeleteTableResponsePB;
using master::GetLeaderMasterRpc;
using master::GetTableSchemaRequestPB;
using master::GetTableSchemaResponsePB;
using master::IsAlterTableDoneRequestPB;
using master::IsAlterTableDoneResponsePB;
using master::IsCreateTableDoneRequestPB;
using master::IsCreateTableDoneResponsePB;
using master::ListTablesRequestPB;
using master::ListTablesResponsePB;
using master::ListTabletServersRequestPB;
using master::ListTabletServersResponsePB;
using master::MasterServiceProxy;
using master::MasterErrorPB;
using rpc::Rpc;
using rpc::RpcController;
using strings::Substitute;

namespace client {

using internal::GetTableSchemaRpc;
using internal::RemoteTablet;
using internal::RemoteTabletServer;

Status RetryFunc(const MonoTime& deadline,
                 const string& retry_msg,
                 const string& timeout_msg,
                 const boost::function<Status(const MonoTime&, bool*)>& func) {
  DCHECK(deadline.Initialized());

  MonoTime now = MonoTime::Now(MonoTime::FINE);
  if (deadline.ComesBefore(now)) {
    return Status::TimedOut(timeout_msg);
  }

  double wait_secs = 0.001;
  const double kMaxSleepSecs = 2;
  while (1) {
    MonoTime func_stime = now;
    bool retry = true;
    Status s = func(deadline, &retry);
    if (!retry) {
      return s;
    }
    now = MonoTime::Now(MonoTime::FINE);
    MonoDelta func_time = now.GetDeltaSince(func_stime);

    VLOG(1) << retry_msg << " status=" << s.ToString();
    double secs_remaining = std::numeric_limits<double>::max();
    if (deadline.Initialized()) {
      secs_remaining = deadline.GetDeltaSince(now).ToSeconds();
    }
    wait_secs = std::min(wait_secs * 1.25, kMaxSleepSecs);

    // We assume that the function will take the same amount of time to run
    // as it did in the previous attempt. If we don't have enough time left
    // to sleep and run it again, we don't bother sleeping and retrying.
    if (wait_secs + func_time.ToSeconds() > secs_remaining) {
      break;
    }

    VLOG(1) << "Waiting for " << HumanReadableElapsedTime::ToShortString(wait_secs)
            << " before retrying...";
    SleepFor(MonoDelta::FromSeconds(wait_secs));
    now = MonoTime::Now(MonoTime::FINE);

  }

  return Status::TimedOut(timeout_msg);
}

template<class ReqClass, class RespClass>
Status KuduClient::Data::SyncLeaderMasterRpc(
    const MonoTime& deadline,
    KuduClient* client,
    const ReqClass& req,
    RespClass* resp,
    int* num_attempts,
    const char* func_name,
    const boost::function<Status(MasterServiceProxy*,
                                 const ReqClass&,
                                 RespClass*,
                                 RpcController*)>& func) {
  DCHECK(deadline.Initialized());

  while (true) {
    RpcController rpc;

    // Have we already exceeded our deadline?
    MonoTime now = MonoTime::Now(MonoTime::FINE);
    if (deadline.ComesBefore(now)) {
      return Status::TimedOut(Substitute("$0 timed out after deadline expired",
                                         func_name));
    }

    // The RPC's deadline is intentionally earlier than the overall
    // deadline so that we reserve some time with which to find a new
    // leader master and retry before the overall deadline expires.
    //
    // TODO: KUDU-683 tracks cleanup for this.
    MonoTime rpc_deadline = now;
    rpc_deadline.AddDelta(client->default_rpc_timeout());
    rpc.set_deadline(MonoTime::Earliest(rpc_deadline, deadline));

    if (num_attempts != nullptr) {
      ++*num_attempts;
    }
    Status s = func(master_proxy_.get(), req, resp, &rpc);
    if (s.IsNetworkError()) {
      LOG(WARNING) << "Unable to send the request (" << req.ShortDebugString()
                   << ") to leader Master (" << leader_master_hostport().ToString()
                   << "): " << s.ToString();
      if (client->IsMultiMaster()) {
        LOG(INFO) << "Determining the new leader Master and retrying...";
        WARN_NOT_OK(SetMasterServerProxy(client, deadline),
                    "Unable to determine the new leader Master");
        continue;
      }
    }

    if (s.IsTimedOut()) {
      if (MonoTime::Now(MonoTime::FINE).ComesBefore(deadline)) {
        LOG(WARNING) << "Unable to send the request (" << req.ShortDebugString()
                     << ") to leader Master (" << leader_master_hostport().ToString()
                     << "): " << s.ToString();
        if (client->IsMultiMaster()) {
          LOG(INFO) << "Determining the new leader Master and retrying...";
          WARN_NOT_OK(SetMasterServerProxy(client, deadline),
                      "Unable to determine the new leader Master");
          continue;
        }
      } else {
        // Operation deadline expired during this latest RPC.
        s = s.CloneAndPrepend(Substitute("$0 timed out after deadline expired",
                                         func_name));
      }
    }

    if (s.ok() && resp->has_error()) {
      if (resp->error().code() == MasterErrorPB::NOT_THE_LEADER ||
          resp->error().code() == MasterErrorPB::CATALOG_MANAGER_NOT_INITIALIZED) {
        if (client->IsMultiMaster()) {
          LOG(INFO) << "Determining the new leader Master and retrying...";
          WARN_NOT_OK(SetMasterServerProxy(client, deadline),
                      "Unable to determine the new leader Master");
          continue;
        }
      }
    }
    return s;
  }
}

// Explicit specialization for callers outside this compilation unit.
template
Status KuduClient::Data::SyncLeaderMasterRpc(
    const MonoTime& deadline,
    KuduClient* client,
    const ListTablesRequestPB& req,
    ListTablesResponsePB* resp,
    int* num_attempts,
    const char* func_name,
    const boost::function<Status(MasterServiceProxy*,
                                 const ListTablesRequestPB&,
                                 ListTablesResponsePB*,
                                 RpcController*)>& func);
template
Status KuduClient::Data::SyncLeaderMasterRpc(
    const MonoTime& deadline,
    KuduClient* client,
    const ListTabletServersRequestPB& req,
    ListTabletServersResponsePB* resp,
    int* num_attempts,
    const char* func_name,
    const boost::function<Status(MasterServiceProxy*,
                                 const ListTabletServersRequestPB&,
                                 ListTabletServersResponsePB*,
                                 RpcController*)>& func);

KuduClient::Data::Data()
    : latest_observed_timestamp_(KuduClient::kNoTimestamp) {
}

KuduClient::Data::~Data() {
  // Workaround for KUDU-956: the user may close a KuduClient while a flush
  // is still outstanding. In that case, the flush's callback will be the last
  // holder of the client reference, causing it to shut down on the reactor
  // thread. This triggers a ThreadRestrictions crash. It's not critical to
  // fix urgently, because typically once a client is shutting down, latency
  // jitter on the reactor is not a big deal (and DNS resolutions are not in flight).
  ThreadRestrictions::ScopedAllowWait allow_wait;
  dns_resolver_.reset();
}

RemoteTabletServer* KuduClient::Data::SelectTServer(const scoped_refptr<RemoteTablet>& rt,
                                                    const ReplicaSelection selection,
                                                    const set<string>& blacklist,
                                                    vector<RemoteTabletServer*>* candidates) const {
  RemoteTabletServer* ret = nullptr;
  candidates->clear();
  switch (selection) {
    case LEADER_ONLY: {
      ret = rt->LeaderTServer();
      if (ret != nullptr) {
        candidates->push_back(ret);
        if (ContainsKey(blacklist, ret->permanent_uuid())) {
          ret = nullptr;
        }
      }
      break;
    }
    case CLOSEST_REPLICA:
    case FIRST_REPLICA: {
      rt->GetRemoteTabletServers(candidates);
      // Filter out all the blacklisted candidates.
      vector<RemoteTabletServer*> filtered;
      for (RemoteTabletServer* rts : *candidates) {
        if (!ContainsKey(blacklist, rts->permanent_uuid())) {
          filtered.push_back(rts);
        } else {
          VLOG(1) << "Excluding blacklisted tserver " << rts->permanent_uuid();
        }
      }
      if (selection == FIRST_REPLICA) {
        if (!filtered.empty()) {
          ret = filtered[0];
        }
      } else if (selection == CLOSEST_REPLICA) {
        // Choose a local replica.
        for (RemoteTabletServer* rts : filtered) {
          if (IsTabletServerLocal(*rts)) {
            ret = rts;
            break;
          }
        }
        // Fallback to a random replica if none are local.
        if (ret == nullptr && !filtered.empty()) {
          ret = filtered[rand() % filtered.size()];
        }
      }
      break;
    }
    default: {
      LOG(FATAL) << "Unknown ProxySelection value " << selection;
      break;
    }
  }

  return ret;
}

Status KuduClient::Data::GetTabletServer(KuduClient* client,
                                         const scoped_refptr<RemoteTablet>& rt,
                                         ReplicaSelection selection,
                                         const set<string>& blacklist,
                                         vector<RemoteTabletServer*>* candidates,
                                         RemoteTabletServer** ts) {
  // TODO: write a proper async version of this for async client.
  RemoteTabletServer* ret = SelectTServer(rt, selection, blacklist, candidates);
  if (PREDICT_FALSE(ret == nullptr)) {
    // Construct a blacklist string if applicable.
    string blacklist_string = "";
    if (!blacklist.empty()) {
      blacklist_string = Substitute("(blacklist replicas $0)", JoinStrings(blacklist, ", "));
    }
    return Status::ServiceUnavailable(
        Substitute("No $0 for tablet $1 $2",
                   selection == LEADER_ONLY ? "LEADER" : "replicas",
                   rt->tablet_id(),
                   blacklist_string));
  }
  Synchronizer s;
  ret->InitProxy(client, s.AsStatusCallback());
  RETURN_NOT_OK(s.Wait());

  *ts = ret;
  return Status::OK();
}

Status KuduClient::Data::CreateTable(KuduClient* client,
                                     const CreateTableRequestPB& req,
                                     const KuduSchema& schema,
                                     const MonoTime& deadline) {
  CreateTableResponsePB resp;

  int attempts = 0;
  Status s = SyncLeaderMasterRpc<CreateTableRequestPB, CreateTableResponsePB>(
      deadline, client, req, &resp, &attempts, "CreateTable", &MasterServiceProxy::CreateTable);
  RETURN_NOT_OK(s);
  if (resp.has_error()) {
    if (resp.error().code() == MasterErrorPB::TABLE_ALREADY_PRESENT && attempts > 1) {
      // If the table already exists and the number of attempts is >
      // 1, then it means we may have succeeded in creating the
      // table, but client didn't receive the successful
      // response (e.g., due to failure before the successful
      // response could be sent back, or due to a I/O pause or a
      // network blip leading to a timeout, etc...)
      KuduSchema actual_schema;
      string table_id;
      PartitionSchema actual_partition_schema;
      RETURN_NOT_OK_PREPEND(
          GetTableSchema(client, req.name(), deadline, &actual_schema,
                         &actual_partition_schema, &table_id),
          Substitute("Unable to check the schema of table $0", req.name()));
      if (!schema.Equals(actual_schema)) {
        string msg = Substitute("Table $0 already exists with a different "
            "schema. Requested schema was: $1, actual schema is: $2",
            req.name(), schema.schema_->ToString(), actual_schema.schema_->ToString());
        LOG(ERROR) << msg;
        return Status::AlreadyPresent(msg);
      } else {
        PartitionSchema partition_schema;
        RETURN_NOT_OK(PartitionSchema::FromPB(req.partition_schema(),
                                              *schema.schema_, &partition_schema));
        if (!partition_schema.Equals(actual_partition_schema)) {
          string msg = Substitute("Table $0 already exists with a different partition schema. "
              "Requested partition schema was: $1, actual partition schema is: $2",
              req.name(), partition_schema.DebugString(*schema.schema_),
              actual_partition_schema.DebugString(*actual_schema.schema_));
          LOG(ERROR) << msg;
          return Status::AlreadyPresent(msg);
        } else {
          return Status::OK();
        }
      }
    }
    return StatusFromPB(resp.error().status());
  }
  return Status::OK();
}

Status KuduClient::Data::IsCreateTableInProgress(KuduClient* client,
                                                 const string& table_name,
                                                 const MonoTime& deadline,
                                                 bool *create_in_progress) {
  IsCreateTableDoneRequestPB req;
  IsCreateTableDoneResponsePB resp;
  req.mutable_table()->set_table_name(table_name);

  // TODO: Add client rpc timeout and use 'default_admin_operation_timeout_' as
  // the default timeout for all admin operations.
  Status s =
      SyncLeaderMasterRpc<IsCreateTableDoneRequestPB, IsCreateTableDoneResponsePB>(
          deadline,
          client,
          req,
          &resp,
          nullptr,
          "IsCreateTableDone",
          &MasterServiceProxy::IsCreateTableDone);
  // RETURN_NOT_OK macro can't take templated function call as param,
  // and SyncLeaderMasterRpc must be explicitly instantiated, else the
  // compiler complains.
  RETURN_NOT_OK(s);
  if (resp.has_error()) {
    return StatusFromPB(resp.error().status());
  }

  *create_in_progress = !resp.done();
  return Status::OK();
}

Status KuduClient::Data::WaitForCreateTableToFinish(KuduClient* client,
                                                    const string& table_name,
                                                    const MonoTime& deadline) {
  return RetryFunc(deadline,
                   "Waiting on Create Table to be completed",
                   "Timed out waiting for Table Creation",
                   boost::bind(&KuduClient::Data::IsCreateTableInProgress,
                               this, client, table_name, _1, _2));
}

Status KuduClient::Data::DeleteTable(KuduClient* client,
                                     const string& table_name,
                                     const MonoTime& deadline) {
  DeleteTableRequestPB req;
  DeleteTableResponsePB resp;
  int attempts = 0;

  req.mutable_table()->set_table_name(table_name);
  Status s = SyncLeaderMasterRpc<DeleteTableRequestPB, DeleteTableResponsePB>(
      deadline, client, req, &resp,
      &attempts, "DeleteTable", &MasterServiceProxy::DeleteTable);
  RETURN_NOT_OK(s);
  if (resp.has_error()) {
    if (resp.error().code() == MasterErrorPB::TABLE_NOT_FOUND && attempts > 1) {
      // A prior attempt to delete the table has succeeded, but
      // appeared as a failure to the client due to, e.g., an I/O or
      // network issue.
      return Status::OK();
    }
    return StatusFromPB(resp.error().status());
  }
  return Status::OK();
}

Status KuduClient::Data::AlterTable(KuduClient* client,
                                    const AlterTableRequestPB& req,
                                    const MonoTime& deadline) {
  AlterTableResponsePB resp;
  Status s =
      SyncLeaderMasterRpc<AlterTableRequestPB, AlterTableResponsePB>(
          deadline,
          client,
          req,
          &resp,
          nullptr,
          "AlterTable",
          &MasterServiceProxy::AlterTable);
  RETURN_NOT_OK(s);
  // TODO: Consider the situation where the request is sent to the
  // server, gets executed on the server and written to the server,
  // but is seen as failed by the client, and is then retried (in which
  // case the retry will fail due to original table being removed, a
  // column being already added, etc...)
  if (resp.has_error()) {
    return StatusFromPB(resp.error().status());
  }
  return Status::OK();
}

Status KuduClient::Data::IsAlterTableInProgress(KuduClient* client,
                                                const string& table_name,
                                                const MonoTime& deadline,
                                                bool *alter_in_progress) {
  IsAlterTableDoneRequestPB req;
  IsAlterTableDoneResponsePB resp;

  req.mutable_table()->set_table_name(table_name);
  Status s =
      SyncLeaderMasterRpc<IsAlterTableDoneRequestPB, IsAlterTableDoneResponsePB>(
          deadline,
          client,
          req,
          &resp,
          nullptr,
          "IsAlterTableDone",
          &MasterServiceProxy::IsAlterTableDone);
  RETURN_NOT_OK(s);
  if (resp.has_error()) {
    return StatusFromPB(resp.error().status());
  }

  *alter_in_progress = !resp.done();
  return Status::OK();
}

Status KuduClient::Data::WaitForAlterTableToFinish(KuduClient* client,
                                                   const string& alter_name,
                                                   const MonoTime& deadline) {
  return RetryFunc(deadline,
                   "Waiting on Alter Table to be completed",
                   "Timed out waiting for AlterTable",
                   boost::bind(&KuduClient::Data::IsAlterTableInProgress,
                               this,
                               client, alter_name, _1, _2));
}

Status KuduClient::Data::InitLocalHostNames() {
  // Currently, we just use our configured hostname, and resolve it to come up with
  // a list of potentially local hosts. It would be better to iterate over all of
  // the local network adapters. See KUDU-327.
  string hostname;
  RETURN_NOT_OK(GetFQDN(&hostname));

  // We don't want to consider 'localhost' to be local - otherwise if a misconfigured
  // server reports its own name as localhost, all clients will hammer it.
  if (hostname != "localhost" && hostname != "localhost.localdomain") {
    local_host_names_.insert(hostname);
    VLOG(1) << "Considering host " << hostname << " local";
  }

  vector<Sockaddr> addresses;
  RETURN_NOT_OK_PREPEND(HostPort(hostname, 0).ResolveAddresses(&addresses),
                        Substitute("Could not resolve local host name '$0'", hostname));

  for (const Sockaddr& addr : addresses) {
    // Similar to above, ignore local or wildcard addresses.
    if (addr.IsWildcard()) continue;
    if (addr.IsAnyLocalAddress()) continue;

    VLOG(1) << "Considering host " << addr.host() << " local";
    local_host_names_.insert(addr.host());
  }

  return Status::OK();
}

bool KuduClient::Data::IsLocalHostPort(const HostPort& hp) const {
  return ContainsKey(local_host_names_, hp.host());
}

bool KuduClient::Data::IsTabletServerLocal(const RemoteTabletServer& rts) const {
  vector<HostPort> host_ports;
  rts.GetHostPorts(&host_ports);
  for (const HostPort& hp : host_ports) {
    if (IsLocalHostPort(hp)) return true;
  }
  return false;
}

namespace internal {

// Gets a table's schema from the leader master. If the leader master
// is down, waits for a new master to become the leader, and then gets
// the table schema from the new leader master.
//
// TODO: When we implement the next fault tolerant client-master RPC
// call (e.g., CreateTable/AlterTable), we should generalize this
// method as to enable code sharing.
class GetTableSchemaRpc : public Rpc {
 public:
  GetTableSchemaRpc(KuduClient* client,
                    StatusCallback user_cb,
                    string table_name,
                    KuduSchema* out_schema,
                    PartitionSchema* out_partition_schema,
                    string* out_id,
                    const MonoTime& deadline,
                    const shared_ptr<rpc::Messenger>& messenger);

  virtual void SendRpc() OVERRIDE;

  virtual string ToString() const OVERRIDE;

  virtual ~GetTableSchemaRpc();

 private:
  virtual void SendRpcCb(const Status& status) OVERRIDE;

  void ResetLeaderMasterAndRetry();

  void NewLeaderMasterDeterminedCb(const Status& status);

  KuduClient* client_;
  StatusCallback user_cb_;
  const string table_name_;
  KuduSchema* out_schema_;
  PartitionSchema* out_partition_schema_;
  string* out_id_;
  GetTableSchemaResponsePB resp_;
};

GetTableSchemaRpc::GetTableSchemaRpc(KuduClient* client,
                                     StatusCallback user_cb,
                                     string table_name,
                                     KuduSchema* out_schema,
                                     PartitionSchema* out_partition_schema,
                                     string* out_id,
                                     const MonoTime& deadline,
                                     const shared_ptr<rpc::Messenger>& messenger)
    : Rpc(deadline, messenger),
      client_(DCHECK_NOTNULL(client)),
      user_cb_(std::move(user_cb)),
      table_name_(std::move(table_name)),
      out_schema_(DCHECK_NOTNULL(out_schema)),
      out_partition_schema_(DCHECK_NOTNULL(out_partition_schema)),
      out_id_(DCHECK_NOTNULL(out_id)) {
}

GetTableSchemaRpc::~GetTableSchemaRpc() {
}

void GetTableSchemaRpc::SendRpc() {
  MonoTime now = MonoTime::Now(MonoTime::FINE);
  if (retrier().deadline().ComesBefore(now)) {
    SendRpcCb(Status::TimedOut("GetTableSchema timed out after deadline expired"));
    return;
  }

  // See KuduClient::Data::SyncLeaderMasterRpc().
  MonoTime rpc_deadline = now;
  rpc_deadline.AddDelta(client_->default_rpc_timeout());
  mutable_retrier()->mutable_controller()->set_deadline(
      MonoTime::Earliest(rpc_deadline, retrier().deadline()));

  GetTableSchemaRequestPB req;
  req.mutable_table()->set_table_name(table_name_);
  client_->data_->master_proxy()->GetTableSchemaAsync(
      req, &resp_,
      mutable_retrier()->mutable_controller(),
      boost::bind(&GetTableSchemaRpc::SendRpcCb, this, Status::OK()));
}

string GetTableSchemaRpc::ToString() const {
  return Substitute("GetTableSchemaRpc(table_name: $0, num_attempts: $1)",
                    table_name_, num_attempts());
}

void GetTableSchemaRpc::ResetLeaderMasterAndRetry() {
  client_->data_->SetMasterServerProxyAsync(
      client_,
      retrier().deadline(),
      Bind(&GetTableSchemaRpc::NewLeaderMasterDeterminedCb,
           Unretained(this)));
}

void GetTableSchemaRpc::NewLeaderMasterDeterminedCb(const Status& status) {
  if (status.ok()) {
    mutable_retrier()->mutable_controller()->Reset();
    SendRpc();
  } else {
    LOG(WARNING) << "Failed to determine new Master: " << status.ToString();
    mutable_retrier()->DelayedRetry(this, status);
  }
}

void GetTableSchemaRpc::SendRpcCb(const Status& status) {
  Status new_status = status;
  if (new_status.ok() && mutable_retrier()->HandleResponse(this, &new_status)) {
    return;
  }

  if (new_status.ok() && resp_.has_error()) {
    if (resp_.error().code() == MasterErrorPB::NOT_THE_LEADER ||
        resp_.error().code() == MasterErrorPB::CATALOG_MANAGER_NOT_INITIALIZED) {
      if (client_->IsMultiMaster()) {
        LOG(WARNING) << "Leader Master has changed ("
                     << client_->data_->leader_master_hostport().ToString()
                     << " is no longer the leader), re-trying...";
        ResetLeaderMasterAndRetry();
        return;
      }
    }
    new_status = StatusFromPB(resp_.error().status());
  }

  if (new_status.IsTimedOut()) {
    if (MonoTime::Now(MonoTime::FINE).ComesBefore(retrier().deadline())) {
      if (client_->IsMultiMaster()) {
        LOG(WARNING) << "Leader Master ("
            << client_->data_->leader_master_hostport().ToString()
            << ") timed out, re-trying...";
        ResetLeaderMasterAndRetry();
        return;
      }
    } else {
      // Operation deadline expired during this latest RPC.
      new_status = new_status.CloneAndPrepend(
          "GetTableSchema timed out after deadline expired");
    }
  }

  if (new_status.IsNetworkError()) {
    if (client_->IsMultiMaster()) {
      LOG(WARNING) << "Encountered a network error from the Master("
                   << client_->data_->leader_master_hostport().ToString() << "): "
                   << new_status.ToString() << ", retrying...";
      ResetLeaderMasterAndRetry();
      return;
    }
  }

  if (new_status.ok()) {
    gscoped_ptr<Schema> schema(new Schema());
    new_status = SchemaFromPB(resp_.schema(), schema.get());
    if (new_status.ok()) {
      delete out_schema_->schema_;
      out_schema_->schema_ = schema.release();
      new_status = PartitionSchema::FromPB(resp_.partition_schema(),
                                           *out_schema_->schema_,
                                           out_partition_schema_);

      *out_id_ = resp_.table_id();
      CHECK_GT(out_id_->size(), 0) << "Running against a too-old master";
    }
  }
  if (!new_status.ok()) {
    LOG(WARNING) << ToString() << " failed: " << new_status.ToString();
  }
  user_cb_.Run(new_status);
}

} // namespace internal

Status KuduClient::Data::GetTableSchema(KuduClient* client,
                                        const string& table_name,
                                        const MonoTime& deadline,
                                        KuduSchema* schema,
                                        PartitionSchema* partition_schema,
                                        string* table_id) {
  Synchronizer sync;
  GetTableSchemaRpc rpc(client,
                        sync.AsStatusCallback(),
                        table_name,
                        schema,
                        partition_schema,
                        table_id,
                        deadline,
                        messenger_);
  rpc.SendRpc();
  return sync.Wait();
}

void KuduClient::Data::LeaderMasterDetermined(const Status& status,
                                              const HostPort& host_port) {
  Sockaddr leader_sock_addr;
  Status new_status = status;
  if (new_status.ok()) {
    new_status = SockaddrFromHostPort(host_port, &leader_sock_addr);
  }

  vector<StatusCallback> cbs;
  {
    lock_guard<simple_spinlock> l(&leader_master_lock_);
    cbs.swap(leader_master_callbacks_);
    leader_master_rpc_.reset();

    if (new_status.ok()) {
      leader_master_hostport_ = host_port;
      master_proxy_.reset(new MasterServiceProxy(messenger_, leader_sock_addr));
    }
  }

  for (const StatusCallback& cb : cbs) {
    cb.Run(new_status);
  }
}

Status KuduClient::Data::SetMasterServerProxy(KuduClient* client,
                                              const MonoTime& deadline) {
  Synchronizer sync;
  SetMasterServerProxyAsync(client, deadline, sync.AsStatusCallback());
  return sync.Wait();
}

void KuduClient::Data::SetMasterServerProxyAsync(KuduClient* client,
                                                 const MonoTime& deadline,
                                                 const StatusCallback& cb) {
  DCHECK(deadline.Initialized());

  vector<Sockaddr> master_sockaddrs;
  for (const string& master_server_addr : master_server_addrs_) {
    vector<Sockaddr> addrs;
    Status s;
    // TODO: Do address resolution asynchronously as well.
    s = ParseAddressList(master_server_addr, master::Master::kDefaultPort, &addrs);
    if (!s.ok()) {
      cb.Run(s);
      return;
    }
    if (addrs.empty()) {
      cb.Run(Status::InvalidArgument(Substitute("No master address specified by '$0'",
                                                master_server_addr)));
      return;
    }
    if (addrs.size() > 1) {
      LOG(WARNING) << "Specified master server address '" << master_server_addr << "' "
                   << "resolved to multiple IPs. Using " << addrs[0].ToString();
    }
    master_sockaddrs.push_back(addrs[0]);
  }

  // Finding a new master involves a fan-out RPC to each master. A single
  // RPC timeout's worth of time should be sufficient, though we'll use
  // the provided deadline if it's sooner.
  MonoTime leader_master_deadline = MonoTime::Now(MonoTime::FINE);
  leader_master_deadline.AddDelta(client->default_rpc_timeout());
  MonoTime actual_deadline = MonoTime::Earliest(deadline, leader_master_deadline);

  // This ensures that no more than one GetLeaderMasterRpc is in
  // flight at a time -- there isn't much sense in requesting this information
  // in parallel, since the requests should end up with the same result.
  // Instead, we simply piggy-back onto the existing request by adding our own
  // callback to leader_master_callbacks_.
  unique_lock<simple_spinlock> l(&leader_master_lock_);
  leader_master_callbacks_.push_back(cb);
  if (!leader_master_rpc_) {
    // No one is sending a request yet - we need to be the one to do it.
    leader_master_rpc_.reset(new GetLeaderMasterRpc(
                               Bind(&KuduClient::Data::LeaderMasterDetermined,
                                    Unretained(this)),
                               master_sockaddrs,
                               actual_deadline,
                               messenger_));
    l.unlock();
    leader_master_rpc_->SendRpc();
  }


}

HostPort KuduClient::Data::leader_master_hostport() const {
  lock_guard<simple_spinlock> l(&leader_master_lock_);
  return leader_master_hostport_;
}

shared_ptr<master::MasterServiceProxy> KuduClient::Data::master_proxy() const {
  lock_guard<simple_spinlock> l(&leader_master_lock_);
  return master_proxy_;
}

uint64_t KuduClient::Data::GetLatestObservedTimestamp() const {
  return latest_observed_timestamp_.Load();
}

void KuduClient::Data::UpdateLatestObservedTimestamp(uint64_t timestamp) {
  latest_observed_timestamp_.StoreMax(timestamp);
}

} // namespace client
} // namespace kudu
