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
//
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
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
#ifndef YB_CLIENT_CLIENT_H_
#define YB_CLIENT_CLIENT_H_

#include <stdint.h>

#include <memory>
#include <string>
#include <vector>
#include <utility>
#include <mutex>

#include <boost/function.hpp>
#include <boost/functional/hash/hash.hpp>

#include "yb/client/client_fwd.h"
#include "yb/client/schema.h"
#include "yb/common/common.pb.h"

#ifdef YB_HEADERS_NO_STUBS
#include <gtest/gtest_prod.h>
#include "yb/common/entity_ids.h"
#include "yb/common/index.h"
#include "yb/gutil/macros.h"
#include "yb/gutil/port.h"
#else
#include "yb/client/stubs.h"
#endif
#include "yb/client/yb_table_name.h"

#include "yb/common/partition.h"

#include "yb/rpc/rpc_fwd.h"

#include "yb/util/monotime.h"
#include "yb/util/status.h"
#include "yb/util/status_callback.h"
#include "yb/util/net/net_fwd.h"

template<class T> class scoped_refptr;

namespace yb {

class CloudInfoPB;
class PartitionSchema;
class MetricEntity;

namespace master {
class ReplicationInfoPB;
class TabletLocationsPB;
}

namespace tserver {
class TabletServerServiceProxy;
}

namespace client {

class YBLoggingCallback;
class YBSession;
class YBStatusCallback;
class YBTable;
class YBTableAlterer;
class YBTableCreator;
class YBTabletServer;
class YBValue;
class YBOperation;

namespace internal {
class Batcher;
class GetTableSchemaRpc;
class LookupRpc;
class MetaCache;
class RemoteTablet;
class RemoteTabletServer;
class AsyncRpc;
class TabletInvoker;
}  // namespace internal

// This must match TableType in common.proto.
// We have static_assert's in tablet-test.cc to verify this.
enum YBTableType {
  YQL_TABLE_TYPE = 2,
  REDIS_TABLE_TYPE = 3,
  UNKNOWN_TABLE_TYPE = -1
};

// This needs to be called by a client app before performing any operations that could result in
// logging.
void InitLogging();

//
// Installs a callback for internal client logging. It is invoked for a
// log event of any severity, across any YBClient instance.
//
// Only the first invocation has any effect; subsequent invocations are
// a no-op. The caller must ensure that 'cb' stays alive until
// UninstallLoggingCallback() is called.
//
// Before a callback is registered, all internal client log events are
// logged to stderr.
void InstallLoggingCallback(YBLoggingCallback* cb);

// Removes a callback installed via InstallLoggingCallback().
//
// Only the first invocation has any effect; subsequent invocations are
// a no-op.
//
// Should be called before unloading the client library.
void UninstallLoggingCallback();

// Set the logging verbosity of the client library. By default, this is 0. Logs become
// progressively more verbose as the level is increased. Empirically, the highest
// verbosity level used in YB is 6, which includes very fine-grained tracing
// information. Most useful logging is enabled at level 1 or 2, with the higher levels
// used only in rare circumstances.
//
// Logs are emitted to stderr, or to the configured log callback at SEVERITY_INFO.
//
// This may be called safely at any point during usage of the library.
void SetVerboseLogLevel(int level);

// The YB client library uses signals internally in some cases. By default, it uses
// SIGUSR2. If your application makes use of SIGUSR2, this advanced API can help
// workaround conflicts.
Status SetInternalSignalNumber(int signum);

// Creates a new YBClient with the desired options.
//
// Note that YBClients are shared amongst multiple threads and, as such,
// are stored in shared pointers.
class YBClientBuilder {
 public:
  YBClientBuilder();
  ~YBClientBuilder();

  YBClientBuilder& clear_master_server_addrs();

  // Add RPC addresses of multiple masters.
  YBClientBuilder& master_server_addrs(const std::vector<std::string>& addrs);

  // Add a REST endpoint from which the address of the masters can be queried initially, and
  // refreshed in case of retries. Note that the endpoint mechanism overrides
  // both 'add_master_server_addr_file' and 'add_master_server_addr'.
  YBClientBuilder& add_master_server_endpoint(const std::string& endpoint);

  // Add an RPC address of a master. At least one master is required.
  YBClientBuilder& add_master_server_addr(const std::string& addr);

  // The default timeout used for administrative operations (e.g. CreateTable,
  // AlterTable, ...). Optional.
  //
  // If not provided, defaults to 10s.
  YBClientBuilder& default_admin_operation_timeout(const MonoDelta& timeout);

  // The default timeout for individual RPCs. Optional.
  //
  // If not provided, defaults to 5s.
  YBClientBuilder& default_rpc_timeout(const MonoDelta& timeout);

  // Set the number of reactor threads that are used to send out the requests.
  // (defaults to the flag value yb_client_num_reactors : 4).
  YBClientBuilder& set_num_reactors(int32_t num_reactors);

  // Sets the cloud info for the client, indicating where the client is located.
  YBClientBuilder& set_cloud_info_pb(const CloudInfoPB& cloud_info_pb);

  // Sets metric entity to be used for emitting metrics. Optional.
  YBClientBuilder& set_metric_entity(const scoped_refptr<MetricEntity>& metric_entity);

  // Sets client name to be used for naming the client's messenger/reactors.
  YBClientBuilder& set_client_name(const std::string& name);

  // Sets skip master leader resolution.
  // Used in tests, when we do not have real master.
  YBClientBuilder& set_skip_master_leader_resolution(bool value);

  // Sets the tserver uuid for the client used by the CQL proxy. Intended only for use by CQL
  // proxy clients.
  YBClientBuilder& set_tserver_uuid(const TabletServerId& uuid);

  // Creates the client.
  //
  // The return value may indicate an error in the create operation, or a
  // misuse of the builder; in the latter case, only the last error is
  // returned.
  CHECKED_STATUS Build(std::shared_ptr<YBClient>* client);
 private:
  class Data;

  std::unique_ptr<Data> data_;

  DISALLOW_COPY_AND_ASSIGN(YBClientBuilder);
};

// The YBClient represents a connection to a cluster. From the user
// perspective, they should only need to create one of these in their
// application, likely a singleton -- but it's not a singleton in YB in any
// way. Different Client objects do not interact with each other -- no
// connection pooling, etc. Each YBClient instance is sandboxed with no
// global cross-client state.
//
// In the implementation, the client holds various pieces of common
// infrastructure which is not table-specific:
//
// - RPC messenger: reactor threads and RPC connections are pooled here
// - Authentication: the client is initialized with some credentials, and
//   all accesses through it share those credentials.
// - Caches: caches of table schemas, tablet locations, tablet server IP
//   addresses, etc are shared per-client.
//
// In order to actually access data on the cluster, callers must first
// create a YBSession object using NewSession(). A YBClient may
// have several associated sessions.
//
// TODO: Cluster administration functions are likely to be in this class
// as well.
//
// This class is thread-safe.
class YBClient : public std::enable_shared_from_this<YBClient> {
 public:
  ~YBClient();

  // Creates a YBTableCreator; it is the caller's responsibility to free it.
  YBTableCreator* NewTableCreator();

  // set 'create_in_progress' to true if a CreateTable operation is in-progress
  CHECKED_STATUS IsCreateTableInProgress(const YBTableName& table_name,
                                         bool *create_in_progress);

  // Truncate the specified table.
  // Set 'wait' to true if the call must wait for the table to be fully truncated before returning.
  CHECKED_STATUS TruncateTable(const std::string& table_id, bool wait = true);

  // Delete the specified table.
  // Set 'wait' to true if the call must wait for the table to be fully deleted before returning.
  CHECKED_STATUS DeleteTable(const YBTableName& table_name, bool wait = true);

  // Delete the specified index table.
  // Set 'wait' to true if the call must wait for the table to be fully deleted before returning.
  CHECKED_STATUS DeleteIndexTable(const YBTableName& table_name,
                                  YBTableName* indexed_table_name,
                                  bool wait = true);

  // Creates a YBTableAlterer; it is the caller's responsibility to free it.
  YBTableAlterer* NewTableAlterer(const YBTableName& table_name);

  // set 'alter_in_progress' to true if an AlterTable operation is in-progress
  CHECKED_STATUS IsAlterTableInProgress(const YBTableName& table_name,
                                        bool *alter_in_progress);

  CHECKED_STATUS GetTableSchema(const YBTableName& table_name,
                                YBSchema* schema,
                                PartitionSchema* partition_schema);

  // Namespace related methods.

  // Create a new namespace with the given name.
  CHECKED_STATUS CreateNamespace(const std::string& namespace_name);

  // It calls CreateNamespace(), but before it checks that the namespace has NOT been yet
  // created. So, it prevents error 'namespace already exists'.
  CHECKED_STATUS CreateNamespaceIfNotExists(const std::string& namespace_name);

  // Delete namespace with the given name.
  CHECKED_STATUS DeleteNamespace(const std::string& namespace_name);

  // Grant permission with given arguments.
  CHECKED_STATUS GrantPermission(const PermissionType& permission,
                                 const ResourceType& resource_type,
                                 const std::string& canonical_resource,
                                 const char* resource_name, const char* namespace_name,
                                 const std::string& role_name);
  // List all namespace names.
  // 'namespaces' is appended to only on success.
  CHECKED_STATUS ListNamespaces(std::vector<std::string>* namespaces);

  // Check if the namespace given by 'namespace_name' exists.
  // 'exists' is set only on success.
  CHECKED_STATUS NamespaceExists(const std::string& namespace_name, bool* exists);


  // Authentication and Authorization
  // Create a new role
  CHECKED_STATUS CreateRole(const std::string& role_name,
                            const std::string& salted_hash,
                            const bool login, const bool superuser);

  // Delete a (user-defined) type by name.
  CHECKED_STATUS DeleteRole(const std::string& role_name);

  // (User-defined) type related methods.

  // Create a new (user-defined) type.
  CHECKED_STATUS CreateUDType(const std::string &namespace_name,
                              const std::string &type_name,
                              const std::vector<std::string> &field_names,
                              const std::vector<std::shared_ptr<QLType>> &field_types);

  // Delete a (user-defined) type by name.
  CHECKED_STATUS DeleteUDType(const std::string &namespace_name, const std::string &type_name);

  // Retrieve a (user-defined) type by name.
  CHECKED_STATUS GetUDType(const std::string &namespace_name,
                           const std::string &type_name,
                           std::shared_ptr<QLType> *ql_type);

  // Find the number of tservers. This function should not be called frequently for reading or
  // writing actual data. Currently, it is called only for SQL DDL statements.
  CHECKED_STATUS TabletServerCount(int *tserver_count);

  CHECKED_STATUS ListTabletServers(std::vector<std::unique_ptr<YBTabletServer>>* tablet_servers);

  void AddTabletServerProxy(const std::string& ts_uuid,
                            const std::shared_ptr<tserver::TabletServerServiceProxy>& proxy);

  // List only those tables whose names pass a substring match on 'filter'.
  //
  // 'tables' is appended to only on success.
  CHECKED_STATUS ListTables(std::vector<YBTableName>* tables,
                            const std::string& filter = "");

  // List all running tablets' uuids for this table.
  // 'tablets' is appended to only on success.
  CHECKED_STATUS GetTablets(const YBTableName& table_name,
                            const int32_t max_tablets,
                            std::vector<std::string>* tablet_uuids,
                            std::vector<std::string>* ranges,
                            std::vector<master::TabletLocationsPB>* locations = nullptr);

  CHECKED_STATUS GetTablets(const YBTableName& table_name,
                            const int32_t max_tablets,
                            google::protobuf::RepeatedPtrField<master::TabletLocationsPB>* tablets);

  CHECKED_STATUS GetTabletLocation(const std::string& tablet_id,
                                   master::TabletLocationsPB* tablet_location);

  // Get the list of master uuids. Can be enhanced later to also return port/host info.
  CHECKED_STATUS ListMasters(
    MonoTime deadline,
    std::vector<std::string>* master_uuids);

  // Check if the table given by 'table_name' exists.
  //
  // 'exists' is set only on success.
  CHECKED_STATUS TableExists(const YBTableName& table_name, bool* exists);

  // Open the table with the given name. This will do an RPC to ensure that
  // the table exists and look up its schema.
  //
  // TODO: should we offer an async version of this as well?
  // TODO: probably should have a configurable timeout in YBClientBuilder?
  CHECKED_STATUS OpenTable(const YBTableName& table_name,
                           std::shared_ptr<YBTable>* table);

  // Create a new session for interacting with the cluster.
  // User is responsible for destroying the session object.
  // This is a fully local operation (no RPCs or blocking).
  std::shared_ptr<YBSession> NewSession();

  // Return the socket address of the master leader for this client
  CHECKED_STATUS SetMasterLeaderSocket(Endpoint* leader_socket);

  // Caller knows that the existing leader might have died or stepped down, so it can use this API
  // to reset the client state to point to new master leader.
  CHECKED_STATUS RefreshMasterLeaderSocket(Endpoint* leader_socket);

  // Once a config change is completed to add/remove a master, update the client to add/remove it
  // from its own master address list.
  CHECKED_STATUS AddMasterToClient(const Endpoint& add);
  CHECKED_STATUS RemoveMasterFromClient(const Endpoint& remove);

  // Policy with which to choose amongst multiple replicas.
  enum ReplicaSelection {
    // Select the LEADER replica.
    LEADER_ONLY,

    // Select the closest replica to the client, or a random one if all
    // replicas are equidistant.
    CLOSEST_REPLICA,

    // Select the first replica in the list.
    FIRST_REPLICA
  };

  bool IsMultiMaster() const;

  const MonoDelta& default_admin_operation_timeout() const;
  const MonoDelta& default_rpc_timeout() const;

  // Value for the latest observed hybrid_time when none has been observed or set.
  static const uint64_t kNoHybridTime;

  // Returns highest hybrid_time observed by the client.
  // The latest observed hybrid_time can be used to start a snapshot scan on a
  // table which is guaranteed to contain all data written or previously read by
  // this client.
  uint64_t GetLatestObservedHybridTime() const;

  // Sets the latest observed hybrid_time, encoded in the HybridTime format.
  // This is only useful when forwarding hybrid_times between clients to enforce
  // external consistency when using YBSession::CLIENT_PROPAGATED external consistency
  // mode.
  // To use this the user must obtain the HybridTime encoded hybrid_time from the first
  // client with YBClient::GetLatestObservedHybridTime() and the set it in the new
  // client with this method.
  void SetLatestObservedHybridTime(uint64_t ht_hybrid_time);

  // Given a host and port for a master, get the uuid of that process.
  CHECKED_STATUS GetMasterUUID(const std::string& host, int16_t port, std::string* uuid);

  CHECKED_STATUS SetReplicationInfo(const master::ReplicationInfoPB& replication_info);

  const std::string& client_id() const { return client_id_; }

  void LookupTabletByKey(const YBTable* table,
                         const std::string& partition_key,
                         const MonoTime& deadline,
                         internal::RemoteTabletPtr* remote_tablet,
                         const StatusCallback& callback);

  void LookupTabletById(const std::string& tablet_id,
                        const MonoTime& deadline,
                        internal::RemoteTabletPtr* remote_tablet,
                        const StatusCallback& callback);

  const std::shared_ptr<rpc::Messenger>& messenger() const;

 private:
  class Data;

  friend class YBClientBuilder;
  friend class YBNoOp;
  friend class YBTable;
  friend class YBTableAlterer;
  friend class YBTableCreator;
  friend class internal::Batcher;
  friend class internal::GetTableSchemaRpc;
  friend class internal::LookupRpc;
  friend class internal::MetaCache;
  friend class internal::RemoteTablet;
  friend class internal::RemoteTabletServer;
  friend class internal::AsyncRpc;
  friend class internal::TabletInvoker;
  friend class PlacementInfoTest;

  FRIEND_TEST(ClientTest, TestGetTabletServerBlacklist);
  FRIEND_TEST(ClientTest, TestMasterDown);
  FRIEND_TEST(ClientTest, TestMasterLookupPermits);
  FRIEND_TEST(ClientTest, TestReplicatedMultiTabletTableFailover);
  FRIEND_TEST(ClientTest, TestReplicatedTabletWritesWithLeaderElection);
  FRIEND_TEST(ClientTest, TestScanFaultTolerance);
  FRIEND_TEST(ClientTest, TestScanTimeout);
  FRIEND_TEST(ClientTest, TestWriteWithDeadMaster);
  FRIEND_TEST(MasterFailoverTest, DISABLED_TestPauseAfterCreateTableIssued);

  YBClient();

  // Owned.
  Data* data_;

  // Unique identifier for this client. This will be constant for the lifetime of this client
  // instance and is used in cases such as the load tester, for binding reads and writes from the
  // same client to the same data.
  const std::string client_id_;

  DISALLOW_COPY_AND_ASSIGN(YBClient);
};

class YBMetaDataCache {
 public:
  explicit YBMetaDataCache(std::shared_ptr<YBClient> client) : client_(client) {}

  // Opens the table with the given name. If the table has been opened before, returns the
  // previously opened table from cached_tables_. If the table has not been opened before
  // in this client, this will do an RPC to ensure that the table exists and look up its schema.
  CHECKED_STATUS GetTable(
      const YBTableName& table_name, std::shared_ptr<YBTable>* table, bool* cache_used);

  // Remove the table from cached_tables_ if it is in the cache.
  void RemoveCachedTable(const YBTableName& table_name);

  // Opens the type with the given name. If the type has been opened before, returns the
  // previously opened type from cached_types_. If the type has not been opened before
  // in this client, this will do an RPC to ensure that the type exists and look up its info.
  CHECKED_STATUS GetUDType(const string &keyspace_name,
                           const string &type_name,
                           std::shared_ptr<QLType> *ql_type,
                           bool *cache_used);

  // Remove the type from cached_types_ if it is in the cache.
  void RemoveCachedUDType(const string& keyspace_name, const string& type_name);

 private:
  std::shared_ptr<YBClient> client_;

  // Map from table-name to YBTable instances.
  typedef std::unordered_map<YBTableName,
                             std::shared_ptr<YBTable>,
                             boost::hash<YBTableName>> YBTableMap;
  YBTableMap cached_tables_;
  std::mutex cached_tables_mutex_;

  // Map from type-name to QLType instances.
  typedef std::unordered_map<std::pair<string, string>,
                             std::shared_ptr<QLType>,
                             boost::hash<std::pair<string, string>>> YBTypeMap;
  YBTypeMap cached_types_;
  std::mutex cached_types_mutex_;
};

// Creates a new table with the desired options.
class YBTableCreator {
 public:
  ~YBTableCreator();

  // Sets the name to give the table. It is copied. Required.
  YBTableCreator& table_name(const YBTableName& name);

  // Sets the type of the table.
  YBTableCreator& table_type(YBTableType table_type);

  // Sets the partition hash schema.
  YBTableCreator& hash_schema(YBHashSchema hash_schema);

  // Number of tablets that should be used for this table. If tablet_count is not given, YBClient
  // will calculate this value (num_shards_per_tserver * num_of_tservers).
  YBTableCreator& num_tablets(int32_t count);

  // Sets the schema with which to create the table. Must remain valid for
  // the lifetime of the builder. Required.
  YBTableCreator& schema(const YBSchema* schema);

  // Adds a set of hash partitions to the table.
  //
  // For each set of hash partitions added to the table, the total number of
  // table partitions is multiplied by the number of buckets. For example, if a
  // table is created with 3 split rows, and two hash partitions with 4 and 5
  // buckets respectively, the total number of table partitions will be 80
  // (4 range partitions * 4 hash buckets * 5 hash buckets).
  YBTableCreator& add_hash_partitions(const std::vector<std::string>& columns,
                                        int32_t num_buckets);

  // Adds a set of hash partitions to the table.
  //
  // This constructor takes a seed value, which can be used to randomize the
  // mapping of rows to hash buckets. Setting the seed may provide some
  // amount of protection against denial of service attacks when the hashed
  // columns contain user provided values.
  YBTableCreator& add_hash_partitions(const std::vector<std::string>& columns,
                                        int32_t num_buckets, int32_t seed);

  // Sets the columns on which the table will be range-partitioned.
  //
  // Every column must be a part of the table's primary key. If not set, the
  // table will be created with the primary-key columns as the range-partition
  // columns. If called with an empty vector, the table will be created without
  // range partitioning.
  //
  // Optional.
  YBTableCreator& set_range_partition_columns(const std::vector<std::string>& columns);

  // Sets the rows on which to pre-split the table.
  // The table creator takes ownership of the rows.
  //
  // If any provided row is missing a value for any of the range partition
  // columns, the logical minimum value for that column type will be used by
  // default.
  //
  // If not provided, no range-based pre-splitting is performed.
  //
  // Optional.
  YBTableCreator& split_rows(const std::vector<const YBPartialRow*>& split_rows);

  // Sets the number of replicas for each tablet in the table.
  // This should be an odd number. Optional.
  //
  // If not provided (or if <= 0), falls back to the server-side default.
  YBTableCreator& num_replicas(int n_replicas);

  // For index table: sets the indexed table id of this index.
  YBTableCreator& indexed_table_id(const std::string& id);

  // Set the timeout for the operation. This includes any waiting
  // after the create has been submitted (i.e if the create is slow
  // to be performed for a large table, it may time out and then
  // later be successful).
  YBTableCreator& timeout(const MonoDelta& timeout);

  // Wait for the table to be fully created before returning.
  // Optional.
  //
  // If not provided, defaults to true.
  YBTableCreator& wait(bool wait);

  YBTableCreator& replication_info(const master::ReplicationInfoPB& ri);

  // Creates the table.
  //
  // The return value may indicate an error in the create table operation,
  // or a misuse of the builder; in the latter case, only the last error is
  // returned.
  CHECKED_STATUS Create();
 private:
  class Data;

  friend class YBClient;

  explicit YBTableCreator(YBClient* client);

  // Owned.
  Data* data_;

  DISALLOW_COPY_AND_ASSIGN(YBTableCreator);
};

// A YBTable represents a table on a particular cluster. It holds the current
// schema of the table. Any given YBTable instance belongs to a specific YBClient
// instance.
//
// Upon construction, the table is looked up in the catalog (or catalog cache),
// and the schema fetched for introspection.
//
// This class is thread-safe.
class YBTable : public std::enable_shared_from_this<YBTable> {
 public:
  ~YBTable();

  const YBTableName& name() const;

  YBTableType table_type() const;

  // Return the table's ID. This is an internal identifier which uniquely
  // identifies a table. If the table is deleted and recreated with the same
  // name, the ID will distinguish the old table from the new.
  const std::string& id() const;

  const YBSchema& schema() const;
  const Schema& InternalSchema() const;

  const std::vector<IndexInfo>& indexes() const;

  // Create a new QL operation for this table.
  YBqlWriteOp* NewQLWrite();
  YBqlWriteOp* NewQLInsert();
  YBqlWriteOp* NewQLUpdate();
  YBqlWriteOp* NewQLDelete();

  YBqlReadOp* NewQLRead();
  YBqlReadOp* NewQLSelect();

  YBClient* client() const;

  const PartitionSchema& partition_schema() const;

 private:
  struct Info;
  class Data;

  friend class YBClient;
  friend class internal::GetTableSchemaRpc;

  YBTable(const std::shared_ptr<YBClient>& client, const YBTableName& name, const Info& info);

  // Owned.
  Data* data_;

  DISALLOW_COPY_AND_ASSIGN(YBTable);
};

// Alters an existing table based on the provided steps.
//
// Sample usage:
//   YBTableAlterer* alterer = client->NewTableAlterer("table-name");
//   alterer->AddColumn("foo")->Type(INT32)->NotNull();
//   Status s = alterer->Alter();
//   delete alterer;
class YBTableAlterer {
 public:
  ~YBTableAlterer();

  // Renames the table.
  // If there is no new namespace (only the new table name provided), that means that the table
  // namespace must not be changed (changing the table name only in the same namespace).
  YBTableAlterer* RenameTo(const YBTableName& new_name);

  // Adds a new column to the table.
  //
  // When adding a column, you must specify the default value of the new
  // column using YBColumnSpec::DefaultValue(...).
  YBColumnSpec* AddColumn(const std::string& name);

  // Alter an existing column.
  YBColumnSpec* AlterColumn(const std::string& name);

  // Drops an existing column from the table.
  YBTableAlterer* DropColumn(const std::string& name);

  // Alter table properties
  YBTableAlterer* SetTableProperties(const TableProperties& table_properties);

  // Set the timeout for the operation. This includes any waiting
  // after the alter has been submitted (i.e if the alter is slow
  // to be performed on a large table, it may time out and then
  // later be successful).
  YBTableAlterer* timeout(const MonoDelta& timeout);

  // Wait for the table to be fully altered before returning.
  //
  // If not provided, defaults to true.
  YBTableAlterer* wait(bool wait);

  // Alters the table.
  //
  // The return value may indicate an error in the alter operation, or a
  // misuse of the builder (e.g. add_column() with default_value=NULL); in
  // the latter case, only the last error is returned.
  CHECKED_STATUS Alter();

 private:
  class Data;
  friend class YBClient;

  YBTableAlterer(YBClient* client, const YBTableName& name);

  // Owned.
  Data* data_;

  DISALLOW_COPY_AND_ASSIGN(YBTableAlterer);
};

// An error which occurred in a given operation. This tracks the operation
// which caused the error, along with whatever the actual error was.
class YBError {
 public:
  YBError(std::shared_ptr<YBOperation> failed_op, const Status& error);
  ~YBError();

  // Return the actual error which occurred.
  const Status& status() const;

  // Return the operation which failed.
  const YBOperation& failed_op() const;

  // In some cases, it's possible that the server did receive and successfully
  // perform the requested operation, but the client can't tell whether or not
  // it was successful. For example, if the call times out, the server may still
  // succeed in processing at a later time.
  //
  // This function returns true if there is some chance that the server did
  // process the operation, and false if it can guarantee that the operation
  // did not succeed.
  bool was_possibly_successful() const;

 private:
  class Data;

  std::unique_ptr<Data> data_;
};

typedef std::vector<std::unique_ptr<YBError>> CollectedErrors;

class YBSessionData;

// A YBSession belongs to a specific YBClient, and represents a context in
// which all read/write data access should take place. Within a session,
// multiple operations may be accumulated and batched together for better
// efficiency. Settings like timeouts, priorities, and trace IDs are also set
// per session.
//
// A YBSession's main purpose is for grouping together multiple data-access
// operations together into batches or transactions. It is important to note
// the distinction between these two:
//
// * A batch is a set of operations which are grouped together in order to
//   amortize fixed costs such as RPC call overhead and round trip times.
//   A batch DOES NOT imply any ACID-like guarantees. Within a batch, some
//   operations may succeed while others fail, and concurrent readers may see
//   partial results. If the client crashes mid-batch, it is possible that some
//   of the operations will be made durable while others were lost.
//
// * In contrast, a transaction is a set of operations which are treated as an
//   indivisible semantic unit, per the usual definitions of database transactions
//   and isolation levels.
//
// NOTE: YB does not currently support transactions! They are only mentioned
// in the above documentation to clarify that batches are not transactional and
// should only be used for efficiency.
//
// YBSession is separate from YBTable because a given batch or transaction
// may span multiple tables. This is particularly important in the future when
// we add ACID support, but even in the context of batching, we may be able to
// coalesce writes to different tables hosted on the same server into the same
// RPC.
//
// YBSession is separate from YBClient because, in a multi-threaded
// application, different threads may need to concurrently execute
// transactions. Similar to a JDBC "session", transaction boundaries will be
// delineated on a per-session basis -- in between a "BeginTransaction" and
// "Commit" call on a given session, all operations will be part of the same
// transaction. Meanwhile another concurrent Session object can safely run
// non-transactional work or other transactions without interfering.
//
// Additionally, there is a guarantee that writes from different sessions do not
// get batched together into the same RPCs -- this means that latency-sensitive
// clients can run through the same YBClient object as throughput-oriented
// clients, perhaps by setting the latency-sensitive session's timeouts low and
// priorities high. Without the separation of batches, a latency-sensitive
// single-row insert might get batched along with 10MB worth of inserts from the
// batch writer, thus delaying the response significantly.
//
// Though we currently do not have transactional support, users will be forced
// to use a YBSession to instantiate reads as well as writes.  This will make
// it more straight-forward to add RW transactions in the future without
// significant modifications to the API.
//
// Users who are familiar with the Hibernate ORM framework should find this
// concept of a Session familiar.
//
// This class is not thread-safe.
class YBSession : public std::enable_shared_from_this<YBSession> {
 public:
  explicit YBSession(const std::shared_ptr<YBClient>& client,
                     const YBTransactionPtr& transaction = nullptr);

  ~YBSession();

  enum FlushMode {
    // Every write will be sent to the server in-band with the Apply()
    // call. No batching will occur. This is the default flush mode. In this
    // mode, the Flush() call never has any effect, since each Apply() call
    // has already flushed the buffer. This is the default flush mode.
    AUTO_FLUSH_SYNC,

    // Apply() calls will return immediately, but the writes will be sent in
    // the background, potentially batched together with other writes from
    // the same session. If there is not sufficient buffer space, then Apply()
    // may block for buffer space to be available.
    //
    // Because writes are applied in the background, any errors will be stored
    // in a session-local buffer. Call CountPendingErrors() or GetPendingErrors()
    // to retrieve them.
    // TODO: provide an API for the user to specify a callback to do their own
    // error reporting.
    // TODO: specify which threads the background activity runs on (probably the
    // messenger IO threads?)
    //
    // NOTE: This is not implemented yet, see KUDU-456.
    //
    // The Flush() call can be used to block until the buffer is empty.
    AUTO_FLUSH_BACKGROUND,

    // Apply() calls will return immediately, and the writes will not be
    // sent until the user calls Flush(). If the buffer runs past the
    // configured space limit, then Apply() will return an error.
    MANUAL_FLUSH
  };

  void SetTransaction(YBTransactionPtr transaction);

  // Set the flush mode.
  // REQUIRES: there should be no pending writes -- call Flush() first to ensure.
  CHECKED_STATUS SetFlushMode(FlushMode m) WARN_UNUSED_RESULT;

  // Set the amount of buffer space used by this session for outbound writes.
  // The effect of the buffer size varies based on the flush mode of the
  // session:
  //
  // AUTO_FLUSH_SYNC:
  //   since no buffering is done, this has no effect
  // AUTO_FLUSH_BACKGROUND:
  //   if the buffer space is exhausted, then write calls will block until there
  //   is space available in the buffer.
  // MANUAL_FLUSH:
  //   if the buffer space is exhausted, then write calls will return an error.
  CHECKED_STATUS SetMutationBufferSpace(size_t size) WARN_UNUSED_RESULT;

  // Set the timeout for writes made in this session.
  void SetTimeout(MonoDelta timeout);

  CHECKED_STATUS ReadSync(std::shared_ptr<YBOperation> yb_op) WARN_UNUSED_RESULT;

  void ReadAsync(std::shared_ptr<YBOperation> yb_op, boost::function<void(const Status&)> callback);

  // TODO: add "doAs" ability here for proxy servers to be able to act on behalf of
  // other users, assuming access rights.

  // Apply the write operation.
  //
  // The behavior of this function depends on the current flush mode. Regardless
  // of flush mode, however, Apply may begin to perform processing in the background
  // for the call (e.g looking up the tablet, etc). Given that, an error may be
  // queued into the PendingErrors structure prior to flushing, even in MANUAL_FLUSH
  // mode.
  //
  // In case of any error, which may occur during flushing or because the write_op
  // is malformed, the write_op is stored in the session's error collector which
  // may be retrieved at any time.
  //
  // This is thread safe.
  CHECKED_STATUS Apply(std::shared_ptr<YBOperation> yb_op) WARN_UNUSED_RESULT;

  // Flush any pending writes.
  //
  // Returns a bad status if there are any pending errors after the rows have
  // been flushed. Callers should then use GetPendingErrors to determine which
  // specific operations failed.
  //
  // In AUTO_FLUSH_SYNC mode, this has no effect, since every Apply() call flushes
  // itself inline.
  //
  // In the case that the async version of this method is used, then the callback
  // will be called upon completion of the operations which were buffered since the
  // last flush. In other words, in the following sequence:
  //
  //    session->Insert(a);
  //    session->FlushAsync(callback_1);
  //    session->Insert(b);
  //    session->FlushAsync(callback_2);
  //
  // ... 'callback_2' will be triggered once 'b' has been inserted, regardless of whether
  // 'a' has completed or not.
  //
  // Note that this also means that, if FlushAsync is called twice in succession, with
  // no intervening operations, the second flush will return immediately. For example:
  //
  //    session->Insert(a);
  //    session->FlushAsync(callback_1); // called when 'a' is inserted
  //    session->FlushAsync(callback_2); // called immediately!
  //
  // Note that, as in all other async functions in YB, the callback may be called
  // either from an IO thread or the same thread which calls FlushAsync. The callback
  // should not block.
  //
  // For FlushAsync, 'cb' must remain valid until it is invoked.
  CHECKED_STATUS Flush() WARN_UNUSED_RESULT;
  void FlushAsync(boost::function<void(const Status&)> callback);

  // Abort the unflushed or in-flight operations in the session.
  void Abort();

  // Close the session.
  // Returns an error if there are unflushed or in-flight operations.
  CHECKED_STATUS Close() WARN_UNUSED_RESULT;

  // Return true if there are operations which have not yet been delivered to the
  // cluster. This may include buffered operations (i.e those that have not yet been
  // flushed) as well as in-flight operations (i.e those that are in the process of
  // being sent to the servers).
  // TODO: maybe "incomplete" or "undelivered" is clearer?
  bool HasPendingOperations() const;

  // Return the number of buffered operations. These are operations that have
  // not yet been flushed - i.e they are not en-route yet.
  //
  // Note that this is different than HasPendingOperations() above, which includes
  // operations which have been sent and not yet responded to.
  //
  // This is only relevant in MANUAL_FLUSH mode, where the result will not
  // decrease except for after a manual Flush, after which point it will be 0.
  // In the other flush modes, data is immediately put en-route to the destination,
  // so this will return 0.
  int CountBufferedOperations() const;

  // Return the number of errors which are pending. Errors may accumulate when
  // using the AUTO_FLUSH_BACKGROUND mode.
  int CountPendingErrors() const;

  // Return any errors from previous calls. If there were more errors
  // than could be held in the session's error storage, then sets *overflowed to true.
  //
  // Caller takes ownership of the returned errors.
  CollectedErrors GetPendingErrors();

  // Allow local calls to run in the current thread.
  void set_allow_local_calls_in_curr_thread(bool flag);
  bool allow_local_calls_in_curr_thread() const;

  YBClient* client() const;

 private:
  friend class YBClient;
  friend class internal::Batcher;

  // We need shared_ptr here, because Batcher stored weak_ptr to our data_.
  // But single data is still used only by single YBSession and destroyed with it.
  std::shared_ptr<YBSessionData> data_;

  DISALLOW_COPY_AND_ASSIGN(YBSession);
};

// This class is not thread-safe, though different YBNoOp objects on
// different threads may share a single YBTable object.
class YBNoOp {
 public:
  // Initialize the NoOp request object. The given 'table' object must remain valid
  // for the lifetime of this object.
  explicit YBNoOp(YBTable* table);
  ~YBNoOp();

  // Executes a no-op request against the tablet server on which the row specified
  // by "key" lives.
  CHECKED_STATUS Execute(const YBPartialRow& key);
 private:
  YBTable* table_;

  DISALLOW_COPY_AND_ASSIGN(YBNoOp);
};

// In-memory representation of a remote tablet server.
class YBTabletServer {
 public:
  ~YBTabletServer();

  // Returns the UUID of this tablet server. Is globally unique and
  // guaranteed not to change for the lifetime of the tablet server.
  const std::string& uuid() const;

  // Returns the hostname of the first RPC address that this tablet server
  // is listening on.
  const std::string& hostname() const;

 private:
  class Data;

  friend class YBClient;

  YBTabletServer();

  // Owned.
  Data* data_;

  DISALLOW_COPY_AND_ASSIGN(YBTabletServer);
};

}  // namespace client
}  // namespace yb
#endif  // YB_CLIENT_CLIENT_H_
