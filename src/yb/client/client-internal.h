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
#ifndef YB_CLIENT_CLIENT_INTERNAL_H
#define YB_CLIENT_CLIENT_INTERNAL_H

#include <functional>
#include <set>
#include <string>
#include <unordered_set>
#include <vector>

#include "yb/client/client.h"
#include "yb/util/atomic.h"
#include "yb/util/locks.h"
#include "yb/util/monotime.h"
#include "yb/util/net/net_util.h"

namespace yb {

class DnsResolver;
class HostPort;

namespace master {
class AlterTableRequestPB;
class CreateTableRequestPB;
class GetLeaderMasterRpc;
class MasterServiceProxy;
} // namespace master

namespace rpc {
class Messenger;
class RpcController;
} // namespace rpc

namespace client {

class YBClient::Data {
 public:
  Data();
  ~Data();

  // Selects a TS replica from the given RemoteTablet subject
  // to liveness and the provided selection criteria and blacklist.
  //
  // If no appropriate replica can be found, a non-OK status is returned and 'ts' is untouched.
  //
  // The 'candidates' return parameter indicates tservers that are live and meet the selection
  // criteria, but are possibly filtered by the blacklist. This is useful for implementing
  // retry logic.
  CHECKED_STATUS GetTabletServer(YBClient* client,
                         const scoped_refptr<internal::RemoteTablet>& rt,
                         ReplicaSelection selection,
                         const std::set<std::string>& blacklist,
                         std::vector<internal::RemoteTabletServer*>* candidates,
                         internal::RemoteTabletServer** ts);

  CHECKED_STATUS CreateTable(YBClient* client,
                     const master::CreateTableRequestPB& req,
                     const YBSchema& schema,
                     const MonoTime& deadline);

  CHECKED_STATUS IsCreateTableInProgress(YBClient* client,
                                         const YBTableName& table_name,
                                         const MonoTime& deadline,
                                         bool *create_in_progress);

  CHECKED_STATUS WaitForCreateTableToFinish(YBClient* client,
                                            const YBTableName& table_name,
                                            const MonoTime& deadline);

  CHECKED_STATUS DeleteTable(YBClient* client,
                             const YBTableName& table_name,
                             const MonoTime& deadline,
                             bool wait = true);

  CHECKED_STATUS IsDeleteTableInProgress(YBClient* client,
                                         const std::string& deleted_table_id,
                                         const MonoTime& deadline,
                                         bool *delete_in_progress);

  CHECKED_STATUS WaitForDeleteTableToFinish(YBClient* client,
                                            const std::string& deleted_table_id,
                                            const MonoTime& deadline);

  CHECKED_STATUS AlterTable(YBClient* client,
                    const master::AlterTableRequestPB& req,
                    const MonoTime& deadline);

  CHECKED_STATUS IsAlterTableInProgress(YBClient* client,
                                        const YBTableName& table_name,
                                        const MonoTime& deadline,
                                        bool *alter_in_progress);

  CHECKED_STATUS WaitForAlterTableToFinish(YBClient* client,
                                           const YBTableName& alter_name,
                                           const MonoTime& deadline);

  CHECKED_STATUS GetTableSchema(YBClient* client,
                                const YBTableName& table_name,
                                const MonoTime& deadline,
                                YBSchema* schema,
                                PartitionSchema* partition_schema,
                                std::string* table_id);

  CHECKED_STATUS InitLocalHostNames();

  bool IsLocalHostPort(const HostPort& hp) const;

  bool IsTabletServerLocal(const internal::RemoteTabletServer& rts) const;

  // Returns a non-failed replica of the specified tablet based on the provided selection criteria
  // and tablet server blacklist.
  //
  // Returns NULL if there are no valid tablet servers.
  internal::RemoteTabletServer* SelectTServer(
      const scoped_refptr<internal::RemoteTablet>& rt,
      const ReplicaSelection selection,
      const std::set<std::string>& blacklist,
      std::vector<internal::RemoteTabletServer*>* candidates) const;

  // Sets 'master_proxy_' from the address specified by
  // 'leader_master_hostport_'.  Called by
  // GetLeaderMasterRpc::SendRpcCb() upon successful completion.
  //
  // See also: SetMasterServerProxyAsync.
  void LeaderMasterDetermined(const Status& status,
                              const HostPort& host_port);

  // Asynchronously sets 'master_proxy_' to the leader master by
  // cycling through servers listed in 'master_server_addrs_' until
  // one responds with a Raft configuration that contains the leader
  // master or 'deadline' expires.
  //
  // Invokes 'cb' with the appropriate status when finished.
  //
  // Works with both a distributed and non-distributed configuration.
  void SetMasterServerProxyAsync(YBClient* client,
                                 const MonoTime& deadline,
                                 const StatusCallback& cb);

  // Synchronous version of SetMasterServerProxyAsync method above.
  //
  // NOTE: since this uses a Synchronizer, this may not be invoked by
  // a method that's on a reactor thread.
  //
  // TODO (KUDU-492): Get rid of this method and re-factor the client
  // to lazily initialize 'master_proxy_'.
  CHECKED_STATUS SetMasterServerProxy(YBClient* client,
                              const MonoTime& deadline);

  std::shared_ptr<master::MasterServiceProxy> master_proxy() const;

  HostPort leader_master_hostport() const;

  uint64_t GetLatestObservedHybridTime() const;

  void UpdateLatestObservedHybridTime(uint64_t hybrid_time);

  // API's to add/remove/set the master address list in the client
  CHECKED_STATUS SetMasterAddresses(const std::string& addresses);
  CHECKED_STATUS RemoveMasterAddress(const Sockaddr& sockaddr);
  CHECKED_STATUS AddMasterAddress(const Sockaddr& sockaddr);
  // This method reads the master address from the remote endpoint or a file depending on which is
  // specified, and re-initializes the 'master_server_addrs_' variable.
  CHECKED_STATUS ReinitializeMasterAddresses();

  // Set replication info for the cluster data. Last argument defaults to nullptr to auto-wrap in a
  // retry. It is otherwise used in a RetryFunc to indicate if to keep retrying or not, if we get a
  // version mismatch on setting the config.
  CHECKED_STATUS SetReplicationInfo(
      YBClient* client, const master::ReplicationInfoPB& replication_info, const MonoTime& deadline,
      bool* retry = nullptr);

  // Retry 'func' until either:
  //
  // 1) Methods succeeds on a leader master.
  // 2) Method fails for a reason that is not related to network
  //    errors, timeouts, or leadership issues.
  // 3) 'deadline' (if initialized) elapses.
  //
  // If 'num_attempts' is not NULL, it will be incremented on every
  // attempt (successful or not) to call 'func'.
  //
  // NOTE: 'rpc_timeout' is a per-call timeout, while 'deadline' is a
  // per operation deadline. If 'deadline' is not initialized, 'func' is
  // retried forever. If 'deadline' expires, 'func_name' is included in
  // the resulting Status.
  template <class ReqClass, class RespClass>
  CHECKED_STATUS SyncLeaderMasterRpc(
      const MonoTime& deadline, YBClient* client, const ReqClass& req, RespClass* resp,
      int* num_attempts, const char* func_name,
      const std::function<Status(
          master::MasterServiceProxy*, const ReqClass&, RespClass*, rpc::RpcController*)>& func);

  std::shared_ptr<rpc::Messenger> messenger_;
  gscoped_ptr<DnsResolver> dns_resolver_;
  scoped_refptr<internal::MetaCache> meta_cache_;

  // Set of hostnames and IPs on the local host.
  // This is initialized at client startup.
  std::unordered_set<std::string> local_host_names_;

  // This is a REST endpoint from which the list of master hosts and ports can be queried. This
  // takes precedence over both 'master_server_addrs_file_' and 'master_server_addrs_'.
  std::string master_server_endpoint_;

  // This is a file which contains the master addresses string in it. It is periodically reloaded
  // to update the master addresses. This takes precedence over 'master_server_addrs_'.
  std::string master_server_addrs_file_;

  // This vector holds the list of master server addresses. Note that each entry in this vector
  // can either be a single 'host:port' or a comma separated list of 'host1:port1,host2:port2,...'.
  std::vector<std::string> master_server_addrs_;

  MonoDelta default_admin_operation_timeout_;
  MonoDelta default_rpc_timeout_;

  // The host port of the leader master. This is set in
  // LeaderMasterDetermined, which is invoked as a callback by
  // SetMasterServerProxyAsync.
  HostPort leader_master_hostport_;

  // Proxy to the leader master.
  std::shared_ptr<master::MasterServiceProxy> master_proxy_;

  // Ref-counted RPC instance: since 'SetMasterServerProxyAsync' call
  // is asynchronous, we need to hold a reference in this class
  // itself, as to avoid a "use-after-free" scenario.
  scoped_refptr<master::GetLeaderMasterRpc> leader_master_rpc_;
  std::vector<StatusCallback> leader_master_callbacks_;

  // Protects 'leader_master_rpc_', 'leader_master_hostport_',
  // and master_proxy_
  //
  // See: YBClient::Data::SetMasterServerProxyAsync for a more
  // in-depth explanation of why this is needed and how it works.
  mutable simple_spinlock leader_master_lock_;

  AtomicInt<uint64_t> latest_observed_hybrid_time_;

 private:
  DISALLOW_COPY_AND_ASSIGN(Data);
};

// Retry helper, takes a function like:
//     CHECKED_STATUS funcName(const MonoTime& deadline, bool *retry, ...)
// The function should set the retry flag (default true) if the function should
// be retried again. On retry == false the return status of the function will be
// returned to the caller, otherwise a Status::Timeout() will be returned.
// If the deadline is already expired, no attempt will be made.
Status RetryFunc(
    const MonoTime& deadline, const std::string& retry_msg, const std::string& timeout_msg,
    const std::function<Status(const MonoTime&, bool*)>& func);

} // namespace client
} // namespace yb

#endif  // YB_CLIENT_CLIENT_INTERNAL_H
