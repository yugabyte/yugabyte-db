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
#ifndef YB_MASTER_CATALOG_MANAGER_H
#define YB_MASTER_CATALOG_MANAGER_H

#include <list>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <boost/optional/optional_fwd.hpp>
#include <boost/functional/hash.hpp>

#include "yb/common/entity_ids.h"
#include "yb/common/partition.h"
#include "yb/consensus/consensus.pb.h"
#include "yb/gutil/macros.h"
#include "yb/gutil/ref_counted.h"
#include "yb/gutil/strings/substitute.h"
#include "yb/master/master_defaults.h"
#include "yb/master/master.pb.h"
#include "yb/master/system_tables_handler.h"
#include "yb/master/ts_manager.h"
#include "yb/master/yql_virtual_table.h"
#include "yb/server/monitored_task.h"
#include "yb/tserver/tablet_peer_lookup.h"
#include "yb/util/cow_object.h"
#include "yb/util/locks.h"
#include "yb/util/monotime.h"
#include "yb/util/net/net_util.h"
#include "yb/util/oid_generator.h"
#include "yb/util/promise.h"
#include "yb/util/random.h"
#include "yb/util/rw_mutex.h"
#include "yb/util/status.h"

namespace yb {

class Schema;
class ThreadPool;

template<class T>
class AtomicGauge;

namespace master {

class CatalogManagerBgTasks;
class ClusterLoadBalancer;
class Master;
class SysCatalogTable;
class TableInfo;
class TSDescriptor;

struct DeferredAssignmentActions;

static const char* const kDefaultSysEntryUnusedId = "";

using PlacementId = std::string;

typedef unordered_map<TabletId, TabletServerId> TabletToTabletServerMap;

typedef std::unordered_map<TabletServerId, MonoTime> LeaderStepDownFailureTimes;

// This class is a base wrapper around the protos that get serialized in the data column of the
// sys_catalog. Subclasses of this will provide convenience getter/setter methods around the
// protos and instances of these will be wrapped around CowObjects and locks for access and
// modifications.
template <class DataEntryPB, SysRowEntry::Type entry_type>
struct Persistent {
  // Type declaration to be used in templated read/write methods. We are using typename
  // Class::data_type in templated methods for figuring out the type we need.
  typedef DataEntryPB data_type;

  // Subclasses of this need to provide a valid value of the entry type through
  // the template class argument.
  static SysRowEntry::Type type() { return entry_type; }

  // The proto that is persisted in the sys_catalog.
  DataEntryPB pb;
};

// This class is used to manage locking of the persistent metadata returned from the
// MetadataCowWrapper objects.
template <class MetadataClass>
class MetadataLock : public CowLock<typename MetadataClass::cow_state> {
 public:
  typedef CowLock<typename MetadataClass::cow_state> super;
  MetadataLock(MetadataClass* info, typename super::LockMode mode)
      : super(DCHECK_NOTNULL(info)->mutable_metadata(), mode) {}
  MetadataLock(const MetadataClass* info, typename super::LockMode mode)
      : super(&(DCHECK_NOTNULL(info))->metadata(), mode) {}
};

// This class is a base wrapper around accessors for the persistent proto data, through CowObject.
// The locks are taken on subclasses of this class, around the object returned from metadata().
template <class PersistentDataEntryPB>
class MetadataCowWrapper {
 public:
  // Type declaration for use in the Lock classes.
  typedef PersistentDataEntryPB cow_state;
  typedef MetadataLock<MetadataCowWrapper<PersistentDataEntryPB>> lock_type;

  // This method should return the id to be written into the sys_catalog id column.
  virtual const std::string& id() const = 0;

  // Pretty printing.
  virtual std::string ToString() const {
    return strings::Substitute(
        "Object type = $0 (id = $1)", PersistentDataEntryPB::type(), id());
  }

  // Access the persistent metadata. Typically you should use
  // TabletMetadataLock to gain access to this data.
  const CowObject<PersistentDataEntryPB>& metadata() const { return metadata_; }
  CowObject<PersistentDataEntryPB>* mutable_metadata() { return &metadata_; }

  std::unique_ptr<lock_type> LockForRead() const {
    return std::unique_ptr<lock_type>(new lock_type(this, lock_type::READ));
  }

  std::unique_ptr<lock_type> LockForWrite() {
    return std::unique_ptr<lock_type>(new lock_type(this, lock_type::WRITE));
  }

 protected:
  virtual ~MetadataCowWrapper() = default;
  CowObject<PersistentDataEntryPB> metadata_;
};

// The data related to a tablet which is persisted on disk.
// This portion of TableInfo is managed via CowObject.
// It wraps the underlying protobuf to add useful accessors.
struct PersistentTabletInfo : public Persistent<SysTabletsEntryPB, SysRowEntry::TABLET> {
  bool is_running() const {
    return pb.state() == SysTabletsEntryPB::RUNNING;
  }

  bool is_deleted() const {
    return pb.state() == SysTabletsEntryPB::REPLACED ||
           pb.state() == SysTabletsEntryPB::DELETED;
  }

  // Helper to set the state of the tablet with a custom message.
  // Requires that the caller has prepared this object for write.
  // The change will only be visible after Commit().
  void set_state(SysTabletsEntryPB::State state, const std::string& msg);
};

// Information on a current replica of a tablet.
// This is copyable so that no locking is needed.
struct TabletReplica {
  TSDescriptor* ts_desc;
  tablet::TabletStatePB state;
  consensus::RaftPeerPB::Role role;

  std::string ToString() const {
    return Format("{ ts_desc: $0 state: $1 role: $2 }",
                  ts_desc->permanent_uuid(),
                  tablet::TabletStatePB_Name(state),
                  consensus::RaftPeerPB_Role_Name(role));
  }
};

// The information about a single tablet which exists in the cluster,
// including its state and locations.
//
// This object uses copy-on-write for the portions of data which are persisted
// on disk. This allows the mutated data to be staged and written to disk
// while readers continue to access the previous version. These portions
// of data are in PersistentTableInfo above, and typically accessed using
// TabletMetadataLock. For example:
//
//   TabletInfo* table = ...;
//   TabletMetadataLock l(tablet, TableMetadataLock::READ);
//   if (l.data().is_running()) { ... }
//
// The non-persistent information about the tablet is protected by an internal
// spin-lock.
//
// The object is owned/managed by the CatalogManager, and exposed for testing.
class TabletInfo : public RefCountedThreadSafe<TabletInfo>,
                   public MetadataCowWrapper<PersistentTabletInfo> {
 public:
  typedef std::unordered_map<std::string, TabletReplica> ReplicaMap;

  TabletInfo(const scoped_refptr<TableInfo>& table, TabletId tablet_id);
  virtual const TabletId& id() const override { return tablet_id_; }

  const TabletId& tablet_id() const { return tablet_id_; }
  const scoped_refptr<TableInfo>& table() const { return table_; }

  // Accessors for the latest known tablet replica locations.
  // These locations include only the members of the latest-reported Raft
  // configuration whose tablet servers have ever heartbeated to this Master.
  void SetReplicaLocations(ReplicaMap replica_locations);
  void GetReplicaLocations(ReplicaMap* replica_locations) const;

  // Adds the given replica to the replica_locations_ map.
  // Returns true iff the replica was inserted.
  bool AddToReplicaLocations(const TabletReplica& replica);

  // Accessors for the last time the replica locations were updated.
  void set_last_update_time(const MonoTime& ts);
  MonoTime last_update_time() const;

  // Accessors for the last reported schema version
  bool set_reported_schema_version(uint32_t version);
  uint32_t reported_schema_version() const;

  // No synchronization needed.
  std::string ToString() const override;

  // Determines whether or not this tablet belongs to a system table.
  bool IsSupportedSystemTable(const SystemTableSet& supported_system_tables) const;

  // This is called when a leader stepdown request fails. Optionally, takes an amount of time since
  // the stepdown failure, in case it happened in the past (e.g. we talked to a tablet server and
  // it told us that it previously tried to step down in favor of this server and that server lost
  // the election).
  void RegisterLeaderStepDownFailure(const TabletServerId& intended_leader,
                                     MonoDelta time_since_stepdown_failure);

  // Retrieves a map of recent leader step-down failures. At the same time, forgets about step-down
  // failures that happened before a certain point in time.
  void GetLeaderStepDownFailureTimes(MonoTime forget_failures_before,
                                     LeaderStepDownFailureTimes* dest);
 private:
  friend class RefCountedThreadSafe<TabletInfo>;
  ~TabletInfo();

  const TabletId tablet_id_;
  const scoped_refptr<TableInfo> table_;

  // Lock protecting the below mutable fields.
  // This doesn't protect metadata_ (the on-disk portion).
  mutable simple_spinlock lock_;

  // The last time the replica locations were updated.
  // Also set when the Master first attempts to create the tablet.
  MonoTime last_update_time_;

  // The locations in the latest Raft config where this tablet has been
  // reported. The map is keyed by tablet server UUID.
  ReplicaMap replica_locations_;

  // Reported schema version (in-memory only).
  uint32_t reported_schema_version_ = 0;

  LeaderStepDownFailureTimes leader_stepdown_failure_times_;

  DISALLOW_COPY_AND_ASSIGN(TabletInfo);
};

typedef scoped_refptr<TabletInfo> TabletInfoPtr;
typedef std::vector<TabletInfoPtr> TabletInfos;

// The data related to a table which is persisted on disk.
// This portion of TableInfo is managed via CowObject.
// It wraps the underlying protobuf to add useful accessors.
struct PersistentTableInfo : public Persistent<SysTablesEntryPB, SysRowEntry::TABLE> {
  bool started_deleting() const {
    return pb.state() == SysTablesEntryPB::DELETING ||
           pb.state() == SysTablesEntryPB::DELETED;
  }

  bool is_deleted() const {
    return pb.state() == SysTablesEntryPB::DELETED;
  }

  bool is_running() const {
    return pb.state() == SysTablesEntryPB::RUNNING ||
           pb.state() == SysTablesEntryPB::ALTERING;
  }

  // Return the table's name.
  const TableName& name() const {
    return pb.name();
  }

  // Return the table's type.
  const TableType table_type() const {
    return pb.table_type();
  }

  // Return the table's namespace id.
  const NamespaceName& namespace_id() const { return pb.namespace_id(); }

  const SchemaPB& schema() const {
    return pb.schema();
  }

  // Helper to set the state of the tablet with a custom message.
  void set_state(SysTablesEntryPB::State state, const std::string& msg);
};

// The information about a table, including its state and tablets.
//
// This object uses copy-on-write techniques similarly to TabletInfo.
// Please see the TabletInfo class doc above for more information.
//
// The non-persistent information about the table is protected by an internal
// spin-lock.
class TableInfo : public RefCountedThreadSafe<TableInfo>,
                  public MetadataCowWrapper<PersistentTableInfo> {
 public:
  explicit TableInfo(TableId table_id);

  // Determines whether or not this table is a system table supported by the master
  bool IsSupportedSystemTable(const SystemTableSet& supported_system_tables) const;

  const TableName name() const;

  bool is_running() const;

  std::string ToString() const override;

  const NamespaceId namespace_id() const;

  const CHECKED_STATUS GetSchema(Schema* schema) const;

  // Return the table's ID. Does not require synchronization.
  virtual const std::string& id() const override { return table_id_; }

  // Return the indexed table id if the table is an index table. Otherwise, return an empty string.
  const std::string indexed_table_id() const;

  // Add a tablet to this table.
  void AddTablet(TabletInfo *tablet);
  // Add multiple tablets to this table.
  void AddTablets(const std::vector<TabletInfo*>& tablets);

  // Return true if tablet with 'partition_key_start' has been
  // removed from 'tablet_map_' below.
  bool RemoveTablet(const std::string& partition_key_start);

  // This only returns tablets which are in RUNNING state.
  void GetTabletsInRange(const GetTableLocationsRequestPB* req,
                         TabletInfos *ret) const;

  void GetAllTablets(TabletInfos *ret) const;

  // Get info of the specified index.
  IndexInfo GetIndexInfo(const TableId& index_id) const;

  // Returns true if the table creation is in-progress
  bool IsCreateInProgress() const;

  // Returns true if an "Alter" operation is in-progress
  bool IsAlterInProgress(uint32_t version) const;

  // Set the Status related to errors on CreateTable.
  void SetCreateTableErrorStatus(const Status& status);

  // Get the Status of the last error from the current CreateTable.
  CHECKED_STATUS GetCreateTableErrorStatus() const;

  std::size_t NumTasks() const;
  bool HasTasks() const;
  bool HasTasks(MonitoredTask::Type type) const;
  void AddTask(std::shared_ptr<MonitoredTask> task);
  void RemoveTask(const std::shared_ptr<MonitoredTask>& task);
  void AbortTasks();
  void WaitTasksCompletion();

  // Allow for showing outstanding tasks in the master UI.
  std::unordered_set<std::shared_ptr<MonitoredTask>> GetTasks();

 private:
  friend class RefCountedThreadSafe<TableInfo>;
  ~TableInfo();

  void AddTabletUnlocked(TabletInfo* tablet);

  const TableId table_id_;

  // Sorted index of tablet start partition-keys to TabletInfo.
  // The TabletInfo objects are owned by the CatalogManager.
  typedef std::map<std::string, TabletInfo *> TabletInfoMap;
  TabletInfoMap tablet_map_;

  // Protects tablet_map_ and pending_tasks_
  mutable simple_spinlock lock_;

  // List of pending tasks (e.g. create/alter tablet requests)
  std::unordered_set<std::shared_ptr<MonitoredTask>> pending_tasks_;

  // The last error Status of the currently running CreateTable. Will be OK, if freshly constructed
  // object, or if the CreateTable was successful.
  Status create_table_error_;

  DISALLOW_COPY_AND_ASSIGN(TableInfo);
};

class DeletedTableInfo;
typedef std::pair<TabletServerId, TabletId> TabletKey;
typedef std::unordered_map<
    TabletKey, scoped_refptr<DeletedTableInfo>, boost::hash<TabletKey>> DeletedTabletMap;

class DeletedTableInfo : public RefCountedThreadSafe<DeletedTableInfo> {
 public:
  explicit DeletedTableInfo(const TableInfo* table);

  const TableId& id() const { return table_id_; }

  std::size_t NumTablets() const;
  bool HasTablets() const;

  void DeleteTablet(const TabletKey& key);

  void AddTabletsToMap(DeletedTabletMap* tablet_map);

 private:
  const TableId table_id_;

  // Protects tablet_set_.
  mutable simple_spinlock lock_;

  typedef std::unordered_set<TabletKey, boost::hash<TabletKey>> TabletSet;
  TabletSet tablet_set_;
};

// The data related to a namespace which is persisted on disk.
// This portion of NamespaceInfo is managed via CowObject.
// It wraps the underlying protobuf to add useful accessors.
struct PersistentNamespaceInfo : public Persistent<SysNamespaceEntryPB, SysRowEntry::NAMESPACE> {
  // Get the namespace name
  const NamespaceName& name() const {
    return pb.name();
  }
};

// The information about a namespace.
//
// This object uses copy-on-write techniques similarly to TabletInfo.
// Please see the TabletInfo class doc above for more information.
class NamespaceInfo : public RefCountedThreadSafe<NamespaceInfo>,
                      public MetadataCowWrapper<PersistentNamespaceInfo> {
 public:
  explicit NamespaceInfo(NamespaceId ns_id);

  virtual const std::string& id() const override { return namespace_id_; }

  const NamespaceName& name() const;

  std::string ToString() const override;

 private:
  friend class RefCountedThreadSafe<NamespaceInfo>;
  ~NamespaceInfo() = default;

  // The ID field is used in the sys_catalog table.
  const NamespaceId namespace_id_;

  DISALLOW_COPY_AND_ASSIGN(NamespaceInfo);
};

// The data related to a User-Defined Type which is persisted on disk.
// This portion of UDTypeInfo is managed via CowObject.
// It wraps the underlying protobuf to add useful accessors.
struct PersistentUDTypeInfo : public Persistent<SysUDTypeEntryPB, SysRowEntry::UDTYPE> {
  // Return the type's name.
  const UDTypeName& name() const {
    return pb.name();
  }

  // Return the table's namespace id.
  const NamespaceName& namespace_id() const {
    return pb.namespace_id();
  }

  int field_names_size() const {
    return pb.field_names_size();
  }

  const string& field_names(int index) const {
    return pb.field_names(index);
  }

  int field_types_size() const {
    return pb.field_types_size();
  }

  const QLTypePB& field_types(int index) const {
    return pb.field_types(index);
  }
};

class UDTypeInfo : public RefCountedThreadSafe<UDTypeInfo>,
                   public MetadataCowWrapper<PersistentUDTypeInfo> {
 public:
  explicit UDTypeInfo(UDTypeId udtype_id);

  // Return the user defined type's ID. Does not require synchronization.
  virtual const std::string& id() const override { return udtype_id_; }

  const UDTypeName& name() const;

  const NamespaceName& namespace_id() const;

  int field_names_size() const;

  const string& field_names(int index) const;

  int field_types_size() const;

  const QLTypePB& field_types(int index) const;

  std::string ToString() const override;

 private:
  friend class RefCountedThreadSafe<UDTypeInfo>;
  ~UDTypeInfo() = default;

  // The ID field is used in the sys_catalog table.
  const UDTypeId udtype_id_;

  DISALLOW_COPY_AND_ASSIGN(UDTypeInfo);
};

// This wraps around the proto containing cluster level config information. It will be used for
// CowObject managed access.
struct PersistentClusterConfigInfo : public Persistent<SysClusterConfigEntryPB,
                                                       SysRowEntry::CLUSTER_CONFIG> {
};

// This is the in memory representation of the cluster config information serialized proto data,
// using metadata() for CowObject access.
class ClusterConfigInfo : public RefCountedThreadSafe<ClusterConfigInfo>,
                          public MetadataCowWrapper<PersistentClusterConfigInfo> {
 public:
  ClusterConfigInfo() {}

  virtual const std::string& id() const override { return fake_id_; }

 private:
  friend class RefCountedThreadSafe<ClusterConfigInfo>;
  ~ClusterConfigInfo() = default;

  // We do not use the ID field in the sys_catalog table.
  const std::string fake_id_ = kDefaultSysEntryUnusedId;

  DISALLOW_COPY_AND_ASSIGN(ClusterConfigInfo);
};

struct PersistentRoleInfo : public Persistent<SysRoleEntryPB, SysRowEntry::ROLE> {
};

class RoleInfo : public RefCountedThreadSafe<RoleInfo>,
                 public MetadataCowWrapper<PersistentRoleInfo> {
 public:
  explicit RoleInfo(const std::string& role) : role_(role) {}
  const std::string& id() const override { return role_; }

  static const std::map<PermissionType, const char*>  kPermissionMap;

  static const char* permissionName(PermissionType permission) {
    auto iterator = kPermissionMap.find(permission);
    if (iterator != kPermissionMap.end()) {
      return iterator->second;
    }
    return nullptr;
  }

 private:
  friend class RefCountedThreadSafe<RoleInfo>;
  ~RoleInfo() = default;

  const std::string role_;

  DISALLOW_COPY_AND_ASSIGN(RoleInfo);
};

// Component within the catalog manager which tracks blacklist (decommission) operation
// related information.
class BlacklistState {
 public:
  BlacklistState() { Reset(); }
  ~BlacklistState() {}

  void Reset();

  std::string ToString();

  // Set of blacklisted servers host/ports. Protected by leader_lock_ in catalog manager.
  std::unordered_set<HostPort, HostPortHash> tservers_;
  // In-memory tracker for initial blacklist load.
  int64_t initial_load_;
};

// Convenience typedefs.
typedef std::unordered_map<TabletId, scoped_refptr<TabletInfo>> TabletInfoMap;
typedef std::unordered_map<TableId, scoped_refptr<TableInfo>> TableInfoMap;
typedef std::pair<NamespaceId, TableName> TableNameKey;
typedef std::unordered_map<
    TableNameKey, scoped_refptr<TableInfo>, boost::hash<TableNameKey>> TableInfoByNameMap;

typedef std::unordered_map<UDTypeId, scoped_refptr<UDTypeInfo>> UDTypeInfoMap;
typedef std::pair<NamespaceId, UDTypeName> UDTypeNameKey;
typedef std::unordered_map<
    UDTypeNameKey, scoped_refptr<UDTypeInfo>, boost::hash<UDTypeNameKey>> UDTypeInfoByNameMap;

// The component of the master which tracks the state and location
// of tables/tablets in the cluster.
//
// This is the master-side counterpart of TSTabletManager, which tracks
// the state of each tablet on a given tablet-server.
//
// Thread-safe.
class CatalogManager : public tserver::TabletPeerLookupIf {
 public:

  // Scoped "shared lock" to serialize master leader elections.
  //
  // While in scope, blocks the catalog manager in the event that it becomes
  // the leader of its Raft configuration and needs to reload its persistent
  // metadata. Once destroyed, the catalog manager is unblocked.
  //
  // Usage:
  //
  // void MasterServiceImpl::CreateTable(const CreateTableRequestPB* req,
  //                                     CreateTableResponsePB* resp,
  //                                     rpc::RpcContext* rpc) {
  //   CatalogManager::ScopedLeaderSharedLock l(server_->catalog_manager());
  //   if (!l.CheckIsInitializedAndIsLeaderOrRespond(resp, rpc)) {
  //     return;
  //   }
  //
  //   Status s = server_->catalog_manager()->CreateTable(req, resp, rpc);
  //   CheckRespErrorOrSetUnknown(s, resp);
  //   rpc.RespondSuccess();
  // }
  //
  class ScopedLeaderSharedLock {
   public:
    // Creates a new shared lock, acquiring the catalog manager's leader_lock_
    // for reading in the process. The lock is released when this object is
    // destroyed.
    //
    // 'catalog' must outlive this object.
    explicit ScopedLeaderSharedLock(CatalogManager* catalog);

    // General status of the catalog manager. If not OK (e.g. the catalog
    // manager is still being initialized), all operations are illegal and
    // leader_status() should not be trusted.
    const Status& catalog_status() const { return catalog_status_; }

    // Leadership status of the catalog manager. If not OK, the catalog
    // manager is not the leader, but some operations may still be legal.
    const Status& leader_status() const {
      DCHECK(catalog_status_.ok());
      return leader_status_;
    }

    // First non-OK status of the catalog manager, adhering to the checking
    // order specified above.
    const Status& first_failed_status() const {
      if (!catalog_status_.ok()) {
        return catalog_status_;
      }
      return leader_status_;
    }

    // Check that the catalog manager is initialized. It may or may not be the
    // leader of its Raft configuration.
    //
    // If not initialized, writes the corresponding error to 'resp',
    // responds to 'rpc', and returns false.
    template<typename RespClass>
      bool CheckIsInitializedOrRespond(RespClass* resp, rpc::RpcContext* rpc);

    // Check that the catalog manager is initialized and that it is the leader
    // of its Raft configuration. Initialization status takes precedence over
    // leadership status.
    //
    // If not initialized or if not the leader, writes the corresponding error
    // to 'resp', responds to 'rpc', and returns false.
    template<typename RespClass>
      bool CheckIsInitializedAndIsLeaderOrRespond(RespClass* resp, rpc::RpcContext* rpc);

    // TServer API variant of above class to set appropriate error codes.
    template<typename RespClass>
    bool CheckIsInitializedAndIsLeaderOrRespondTServer(RespClass* resp, rpc::RpcContext* rpc);

   private:
    template<typename RespClass, typename ErrorClass>
    bool CheckIsInitializedAndIsLeaderOrRespondInternal(RespClass* resp, rpc::RpcContext* rpc);
    CatalogManager* catalog_;
    shared_lock<RWMutex> leader_shared_lock_;
    Status catalog_status_;
    Status leader_status_;
  };

  explicit CatalogManager(Master *master);
  virtual ~CatalogManager();

  CHECKED_STATUS Init(bool is_first_run);

  void Shutdown();
  CHECKED_STATUS CheckOnline() const;

  // Create a new Table with the specified attributes
  //
  // The RPC context is provided for logging/tracing purposes,
  // but this function does not itself respond to the RPC.
  CHECKED_STATUS CreateTable(const CreateTableRequestPB* req,
                             CreateTableResponsePB* resp,
                             rpc::RpcContext* rpc);

  // Get the information about an in-progress create operation
  CHECKED_STATUS IsCreateTableDone(const IsCreateTableDoneRequestPB* req,
                                   IsCreateTableDoneResponsePB* resp);

  // Truncate the specified table
  //
  // The RPC context is provided for logging/tracing purposes,
  // but this function does not itself respond to the RPC.
  CHECKED_STATUS TruncateTable(const TruncateTableRequestPB* req,
                               TruncateTableResponsePB* resp,
                               rpc::RpcContext* rpc);

  // Get the information about an in-progress truncate operation
  CHECKED_STATUS IsTruncateTableDone(const IsTruncateTableDoneRequestPB* req,
                                     IsTruncateTableDoneResponsePB* resp);
  // Delete the specified table
  //
  // The RPC context is provided for logging/tracing purposes,
  // but this function does not itself respond to the RPC.
  CHECKED_STATUS DeleteTable(const DeleteTableRequestPB* req,
                             DeleteTableResponsePB* resp,
                             rpc::RpcContext* rpc);

  // Get the information about an in-progress delete operation
  CHECKED_STATUS IsDeleteTableDone(const IsDeleteTableDoneRequestPB* req,
                                   IsDeleteTableDoneResponsePB* resp);

  // Alter the specified table
  //
  // The RPC context is provided for logging/tracing purposes,
  // but this function does not itself respond to the RPC.
  CHECKED_STATUS AlterTable(const AlterTableRequestPB* req,
                            AlterTableResponsePB* resp,
                            rpc::RpcContext* rpc);

  // Get the information about an in-progress alter operation
  CHECKED_STATUS IsAlterTableDone(const IsAlterTableDoneRequestPB* req,
                                  IsAlterTableDoneResponsePB* resp);

  // Get the information about the specified table
  CHECKED_STATUS GetTableSchema(const GetTableSchemaRequestPB* req,
                                GetTableSchemaResponsePB* resp);

  // List all the running tables
  CHECKED_STATUS ListTables(const ListTablesRequestPB* req,
                            ListTablesResponsePB* resp);

  CHECKED_STATUS GetTableLocations(const GetTableLocationsRequestPB* req,
                                   GetTableLocationsResponsePB* resp);

  // Look up the locations of the given tablet. The locations
  // vector is overwritten (not appended to).
  // If the tablet is not found, returns Status::NotFound.
  // If the tablet is not running, returns Status::ServiceUnavailable.
  // Otherwise, returns Status::OK and puts the result in 'locs_pb'.
  // This only returns tablets which are in RUNNING state.
  CHECKED_STATUS GetTabletLocations(const TabletId& tablet_id,
                                    TabletLocationsPB* locs_pb);

  // Retrieves a SystemTablet instance based on the existing system tablets already created in our
  // syscatalog.
  CHECKED_STATUS RetrieveSystemTablet(const TabletId& tablet_id,
                                      std::shared_ptr<tablet::AbstractTablet>* tablet);

  // Handle a tablet report from the given tablet server.
  //
  // The RPC context is provided for logging/tracing purposes,
  // but this function does not itself respond to the RPC.
  CHECKED_STATUS ProcessTabletReport(TSDescriptor* ts_desc,
                                     const TabletReportPB& report,
                                     TabletReportUpdatesPB *report_update,
                                     rpc::RpcContext* rpc);

  // Create a new Namespace with the specified attributes.
  //
  // The RPC context is provided for logging/tracing purposes,
  // but this function does not itself respond to the RPC.
  CHECKED_STATUS CreateNamespace(const CreateNamespaceRequestPB* req,
                                 CreateNamespaceResponsePB* resp,
                                 rpc::RpcContext* rpc);

  // Delete the specified Namespace.
  //
  // The RPC context is provided for logging/tracing purposes,
  // but this function does not itself respond to the RPC.
  CHECKED_STATUS DeleteNamespace(const DeleteNamespaceRequestPB* req,
                                 DeleteNamespaceResponsePB* resp,
                                 rpc::RpcContext* rpc);

  // Grant Permission to a role
  CHECKED_STATUS GrantPermission(const GrantPermissionRequestPB* req,
                                 GrantPermissionResponsePB* resp,
                                 rpc::RpcContext* rpc);

  // List all the current namespaces.
  CHECKED_STATUS ListNamespaces(const ListNamespacesRequestPB* req,
                                ListNamespacesResponsePB* resp);

  // Create a new role for authentication/authorization
  //
  // The RPC context is provided for logging/tracing purposes,
  // but this function does not itself respond to the RPC.
  CHECKED_STATUS CreateRole(const CreateRoleRequestPB* req,
                            CreateRoleResponsePB* resp,
                            rpc::RpcContext* rpc);

  // Delete the role
  //
  // The RPC context is provided for logging/tracing purposes,
  // but this function does not itself respond to the RPC.
  CHECKED_STATUS DeleteRole(const DeleteRoleRequestPB* req,
                            DeleteRoleResponsePB* resp,
                            rpc::RpcContext* rpc);

  // Generic Create Role function for both default roles and user defined roles
  CHECKED_STATUS CreateRoleUnlocked(const std::string& role_name,
                                    const std::string& salted_hash,
                                    const bool login, const bool superuser);


  // Grant one role to another role
  CHECKED_STATUS GrantRole(const GrantRoleRequestPB* req,
                           GrantRoleResponsePB* resp,
                           rpc::RpcContext* rpc);


  // Create a new User-Defined Type with the specified attributes.
  //
  // The RPC context is provided for logging/tracing purposes,
  // but this function does not itself respond to the RPC.
  CHECKED_STATUS CreateUDType(const CreateUDTypeRequestPB* req,
                              CreateUDTypeResponsePB* resp,
                              rpc::RpcContext* rpc);


  // Delete the specified UDType.
  //
  // The RPC context is provided for logging/tracing purposes,
  // but this function does not itself respond to the RPC.
  CHECKED_STATUS DeleteUDType(const DeleteUDTypeRequestPB* req,
                              DeleteUDTypeResponsePB* resp,
                              rpc::RpcContext* rpc);

  // List all user defined types in given namespaces.
  CHECKED_STATUS ListUDTypes(const ListUDTypesRequestPB* req,
                             ListUDTypesResponsePB* resp);

  // Get the info (id, name, namespace, fields names, field types) of a (user-defined) type
  CHECKED_STATUS GetUDTypeInfo(const GetUDTypeInfoRequestPB* req,
                               GetUDTypeInfoResponsePB* resp,
                               rpc::RpcContext* rpc);

  SysCatalogTable* sys_catalog() { return sys_catalog_.get(); }

  // Dump all of the current state about tables and tablets to the
  // given output stream. This is verbose, meant for debugging.
  virtual void DumpState(std::ostream* out, bool on_disk_dump = false) const;

  void SetLoadBalancerEnabled(bool is_enabled);

  // Return the table info for the table with the specified UUID, if it exists.
  scoped_refptr<TableInfo> GetTableInfo(const TableId& table_id);
  scoped_refptr<TableInfo> GetTableInfoUnlocked(const TableId& table_id);

  // Get Table info given namespace id and table name.
  scoped_refptr<TableInfo> GetTableInfoFromNamespaceNameAndTableName(
      const NamespaceName& namespace_name, const TableName& table_name);


  // Return all the available TableInfo. The flag 'includeOnlyRunningTables' determines whether
  // to retrieve all Tables irrespective of their state or just the tables with the state
  // 'RUNNING'. Typically, if you want to retrieve all the live tables in the system, you should
  // set this flag to true.
  void GetAllTables(std::vector<scoped_refptr<TableInfo> > *tables,
                    bool includeOnlyRunningTables = false);

  void GetAllNamespaces(std::vector<scoped_refptr<NamespaceInfo> >* namespaces);

  // Return all the available (user-defined) types.
  void GetAllUDTypes(std::vector<scoped_refptr<UDTypeInfo> >* types);

  NamespaceName GetNamespaceName(const NamespaceId& id) const;

  void GetAllRoles(std::vector<scoped_refptr<RoleInfo>>* roles);

  // Is the table a system table?
  bool IsSystemTable(const TableInfo& table) const;

  // Let the catalog manager know that we have received a response for a delete tablet request,
  // and that we either deleted the tablet successfully, or we received a fatal error.
  void NotifyTabletDeleteFinished(const TabletServerId& tserver_uuid, const TableId& table_id);

  // Used by ConsensusService to retrieve the TabletPeer for a system
  // table specified by 'tablet_id'.
  //
  // See also: TabletPeerLookupIf, ConsensusServiceImpl.
  virtual CHECKED_STATUS GetTabletPeer(const TabletId& tablet_id,
                               scoped_refptr<tablet::TabletPeer>* tablet_peer) const override;

  virtual const NodeInstancePB& NodeInstance() const override;

  bool IsInitialized() const;

  virtual CHECKED_STATUS StartRemoteBootstrap(const consensus::StartRemoteBootstrapRequestPB& req)
      override;

  // Set the current committed config.
  CHECKED_STATUS GetCurrentConfig(consensus::ConsensusStatePB *cpb) const;

  // Return OK if this CatalogManager is a leader in a consensus configuration and if
  // the required leader state (metadata for tables and tablets) has
  // been successfully loaded into memory. CatalogManager must be
  // initialized before calling this method.
  CHECKED_STATUS CheckIsLeaderAndReady() const;

  // Returns this CatalogManager's role in a consensus configuration. CatalogManager
  // must be initialized before calling this method.
  consensus::RaftPeerPB::Role Role() const;

  CHECKED_STATUS PeerStateDump(const vector<consensus::RaftPeerPB>& masters_raft,
                               bool on_disk = false);

  // If we get removed from an existing cluster, leader might ask us to detach ourselves from the
  // cluster. So we enter a shell mode equivalent state, with no bg tasks and no tablet peer
  // nor consensus.
  CHECKED_STATUS GoIntoShellMode();

  // Setters and getters for the cluster config item.
  //
  // To change the cluster config, a client would need to do a client-side read-modify-write by
  // issuing a get for the latest config, obtaining the current valid config (together with its
  // respective version number), modify the values it wants of said config and issuing a write
  // afterwards, without changing the version number. In case the version number does not match
  // on the server, the change will fail and the client will have to retry the get, as someone
  // must have updated the config in the meantime.
  CHECKED_STATUS GetClusterConfig(GetMasterClusterConfigResponsePB* resp);
  CHECKED_STATUS GetClusterConfig(SysClusterConfigEntryPB* config);
  CHECKED_STATUS SetClusterConfig(
      const ChangeMasterClusterConfigRequestPB* req, ChangeMasterClusterConfigResponsePB* resp);

  CHECKED_STATUS GetReplicationFactor(int* num_replicas);
  CHECKED_STATUS GetReplicationFactor(NamespaceName namespace_name, int* num_replicas) {
    // TODO ENG-282 We currently don't support per-namespace replication factor.
    return GetReplicationFactor(num_replicas);
  }

  // Get the percentage of tablets that have been moved off of the black-listed tablet servers.
  CHECKED_STATUS GetLoadMoveCompletionPercent(GetLoadMovePercentResponsePB* resp);

  // API to check if all the live tservers have similar tablet workload.
  CHECKED_STATUS IsLoadBalanced(const IsLoadBalancedRequestPB* req,
                                IsLoadBalancedResponsePB* resp);

  // Clears out the existing metadata ('table_names_map_', 'table_ids_map_',
  // and 'tablet_map_'), loads tables metadata into memory and if successful
  // loads the tablets metadata.
  CHECKED_STATUS VisitSysCatalog();
  virtual CHECKED_STATUS RunLoaders();

  // Waits for the worker queue to finish processing, returns OK if worker queue is idle before
  // the provided timeout, TimedOut Status otherwise.
  CHECKED_STATUS WaitForWorkerPoolTests(
      const MonoDelta& timeout = MonoDelta::FromSeconds(10)) const;

  size_t NumSystemTables() const {
    return sys_tables_handler_.supported_system_tables().size();
  }

  CHECKED_STATUS FindNamespace(const NamespaceIdentifierPB& ns_identifier,
                               scoped_refptr<NamespaceInfo>* ns_info) const;

  void AssertLeaderLockAcquiredForReading() const {
    leader_lock_.AssertAcquiredForReading();
  }

 protected:
  friend class TableLoader;
  friend class TabletLoader;
  friend class NamespaceLoader;
  friend class UDTypeLoader;
  friend class ClusterConfigLoader;
  friend class RoleLoader;

  FRIEND_TEST(SysCatalogTest, TestPrepareDefaultClusterConfig);

  // Called by SysCatalog::SysCatalogStateChanged when this node
  // becomes the leader of a consensus configuration.
  //
  // Executes LoadSysCatalogDataTask below.
  CHECKED_STATUS ElectedAsLeaderCb();

  // Loops and sleeps until one of the following conditions occurs:
  // 1. The current node is the leader master in the current term
  //    and at least one op from the current term is committed. Returns OK.
  // 2. The current node is not the leader master.
  //    Returns IllegalState.
  // 3. The provided timeout expires. Returns TimedOut.
  //
  // This method is intended to ensure that all operations replicated by
  // previous masters are committed and visible to the local node before
  // reading that data, to ensure consistency across failovers.
  CHECKED_STATUS WaitUntilCaughtUpAsLeader(const MonoDelta& timeout);

  // This method is submitted to 'leader_initialization_pool_' by
  // ElectedAsLeaderCb above. It:
  // 1) Acquired 'lock_'
  // 2) Runs the various Visitors defined below
  // 3) Releases 'lock_' and if successful, updates 'leader_ready_term_'
  // to true (under state_lock_).
  void LoadSysCatalogDataTask();

  // Generated the default entry for the cluster config, that is written into sys_catalog on very
  // first leader election of the cluster.
  //
  // Sets the version field of the SysClusterConfigEntryPB to 0.
  CHECKED_STATUS PrepareDefaultClusterConfig();

  CHECKED_STATUS PrepareDefaultNamespaces();

  CHECKED_STATUS PrepareSystemTables();

  CHECKED_STATUS PrepareDefaultRoles();

  template <class T>
  CHECKED_STATUS PrepareSystemTableTemplate(const TableName& table_name,
                                            const NamespaceName& namespace_name,
                                            const NamespaceId& namespace_id);

  CHECKED_STATUS PrepareSystemTable(const TableName& table_name,
                                    const NamespaceName& namespace_name,
                                    const NamespaceId& namespace_id,
                                    const Schema& schema,
                                    YQLVirtualTable* vtable);

  CHECKED_STATUS PrepareNamespace(const NamespaceName& name, const NamespaceId& id);

  CHECKED_STATUS ConsensusStateToTabletLocations(const consensus::ConsensusStatePB& cstate,
                                                 TabletLocationsPB* locs_pb);

  // Creates the table and associated tablet objects in-memory and updates the appropriate
  // catalog manager maps.
  CHECKED_STATUS CreateTableInMemory(const CreateTableRequestPB& req,
                                     const Schema& schema,
                                     const PartitionSchema& partition_schema,
                                     const bool is_copartitioned,
                                     const NamespaceId& namespace_id,
                                     const vector<Partition>& partitions,
                                     vector<TabletInfo*>* tablets,
                                     CreateTableResponsePB* resp,
                                     scoped_refptr<TableInfo>* table);
  CHECKED_STATUS CreateTabletsFromTable(const vector<Partition>& partitions,
                                        const scoped_refptr<TableInfo>& table,
                                        std::vector<TabletInfo*>* tablets);

  // Helper for creating copartitioned table
  CHECKED_STATUS CreateCopartitionedTable(const CreateTableRequestPB req,
                                          CreateTableResponsePB* resp,
                                          rpc::RpcContext* rpc,
                                          Schema schema,
                                          NamespaceId namespace_id);
  // Helper for initializing 'sys_catalog_'. After calling this
  // method, the caller should call WaitUntilRunning() on sys_catalog_
  // WITHOUT holding 'lock_' to wait for consensus to start for
  // sys_catalog_.
  //
  // This method is thread-safe.
  CHECKED_STATUS InitSysCatalogAsync(bool is_first_run);

  // Helper for creating the initial TableInfo state
  // Leaves the table "write locked" with the new info in the
  // "dirty" state field.
  TableInfo* CreateTableInfo(const CreateTableRequestPB& req,
                             const Schema& schema,
                             const PartitionSchema& partition_schema,
                             const NamespaceId& namespace_id);

  // Helper for creating the initial TabletInfo state.
  // Leaves the tablet "write locked" with the new info in the
  // "dirty" state field.
  TabletInfo *CreateTabletInfo(TableInfo* table,
                               const PartitionPB& partition);

  // Add index info to the indexed table.
  CHECKED_STATUS AddIndexInfoToTable(const TableId& indexed_table_id,
                                     const TableId& index_table_id,
                                     const Schema& index_schema,
                                     bool is_local);

  // Delete index info from the indexed table.
  CHECKED_STATUS DeleteIndexInfoFromTable(const TableId& indexed_table_id,
                                          const TableId& index_table_id,
                                          DeleteTableResponsePB* resp);

  // Builds the TabletLocationsPB for a tablet based on the provided TabletInfo.
  // Populates locs_pb and returns true on success.
  // Returns Status::ServiceUnavailable if tablet is not running.
  CHECKED_STATUS BuildLocationsForTablet(const scoped_refptr<TabletInfo>& tablet,
                                         TabletLocationsPB* locs_pb);

  CHECKED_STATUS FindTable(const TableIdentifierPB& table_identifier,
                           scoped_refptr<TableInfo>* table_info);

  // Handle one of the tablets in a tablet reported.
  // Requires that the lock is already held.
  CHECKED_STATUS HandleReportedTablet(TSDescriptor* ts_desc,
                              const ReportedTabletPB& report,
                              ReportedTabletUpdatesPB *report_updates);

  CHECKED_STATUS ResetTabletReplicasFromReportedConfig(const ReportedTabletPB& report,
                                               const scoped_refptr<TabletInfo>& tablet,
                                               TabletInfo::lock_type* tablet_lock,
                                               TableInfo::lock_type* table_lock);

  // Register a tablet server whenever it heartbeats with a consensus configuration. This is
  // needed because we have logic in the Master that states that if a tablet
  // server that is part of a consensus configuration has not heartbeated to the Master yet, we
  // leave it out of the consensus configuration reported to clients.
  // TODO: See if we can remove this logic, as it seems confusing.
  void AddReplicaToTabletIfNotFound(TSDescriptor* ts_desc,
                                    const ReportedTabletPB& report,
                                    const scoped_refptr<TabletInfo>& tablet);

  void NewReplica(TSDescriptor* ts_desc, const ReportedTabletPB& report, TabletReplica* replica);

  // Extract the set of tablets that can be deleted and the set of tablets
  // that must be processed because not running yet.
  void ExtractTabletsToProcess(TabletInfos *tablets_to_delete,
                               TabletInfos *tablets_to_process);

  // Task that takes care of the tablet assignments/creations.
  // Loops through the "not created" tablets and sends a CreateTablet() request.
  CHECKED_STATUS ProcessPendingAssignments(const TabletInfos& tablets);

  // Given 'two_choices', which should be a vector of exactly two elements, select which
  // one is the better choice for a new replica.
  std::shared_ptr<TSDescriptor> PickBetterReplicaLocation(const TSDescriptorVector& two_choices);

  // Select a tablet server from 'ts_descs' on which to place a new replica.
  // Any tablet servers in 'excluded' are not considered.
  // REQUIRES: 'ts_descs' must include at least one non-excluded server.
  std::shared_ptr<TSDescriptor> SelectReplica(
      const TSDescriptorVector& ts_descs,
      const std::set<std::shared_ptr<TSDescriptor>>& excluded);

  // Select N Replicas from online tablet servers (as specified by
  // 'ts_descs') for the specified tablet and populate the consensus configuration
  // object. If 'ts_descs' does not specify enough online tablet
  // servers to select the N replicas, return Status::InvalidArgument.
  //
  // This method is called by "ProcessPendingAssignments()".
  CHECKED_STATUS SelectReplicasForTablet(const TSDescriptorVector& ts_descs, TabletInfo* tablet);

  // Select N Replicas from the online tablet servers that have been chosen to respect the
  // placement information provided. Populate the consensus configuration object with choices and
  // also update the set of selected tablet servers, to not place several replicas on the same TS.
  //
  // This method is called by "SelectReplicasForTablet".
  void SelectReplicas(
      const TSDescriptorVector& ts_descs, int nreplicas, consensus::RaftConfigPB* config,
      std::set<std::shared_ptr<TSDescriptor>>* already_selected_ts);

  void HandleAssignPreparingTablet(TabletInfo* tablet,
                                   DeferredAssignmentActions* deferred);

  // Assign tablets and send CreateTablet RPCs to tablet servers.
  // The out param 'new_tablets' should have any newly-created TabletInfo
  // objects appended to it.
  void HandleAssignCreatingTablet(TabletInfo* tablet,
                                  DeferredAssignmentActions* deferred,
                                  TabletInfos* new_tablets);

  CHECKED_STATUS HandleTabletSchemaVersionReport(TabletInfo *tablet, uint32_t version);

  // Send the create tablet requests to the selected peers of the consensus configurations.
  // The creation is async, and at the moment there is no error checking on the
  // caller side. We rely on the assignment timeout. If we don't see the tablet
  // after the timeout, we regenerate a new one and proceed with a new
  // assignment/creation.
  //
  // This method is part of the "ProcessPendingAssignments()"
  //
  // This must be called after persisting the tablet state as
  // CREATING to ensure coherent state after Master failover.
  void SendCreateTabletRequests(const std::vector<TabletInfo*>& tablets);

  // Send the "alter table request" to all tablets of the specified table.
  void SendAlterTableRequest(const scoped_refptr<TableInfo>& table);

  // Start the background task to send the AlterTable() RPC to the leader for this
  // tablet.
  void SendAlterTabletRequest(const scoped_refptr<TabletInfo>& tablet);

  // Start the background task to send the CopartitionTable() RPC to the leader for this
  // tablet.
  void SendCopartitionTabletRequest(const scoped_refptr<TabletInfo>& tablet,
                                    const scoped_refptr<TableInfo>& table);

  // Send the "truncate table request" to all tablets of the specified table.
  void SendTruncateTableRequest(const scoped_refptr<TableInfo>& table);

  // Start the background task to send the TruncateTable() RPC to the leader for this tablet.
  void SendTruncateTabletRequest(const scoped_refptr<TabletInfo>& tablet);

  // Delete the specified table in memory. The TableInfo, DeletedTableInfo and lock of the deleted
  // table are appended to the lists. The caller will be responsible for committing the change and
  // deleting the actual table and tablets.
  CHECKED_STATUS DeleteTableInMemory(const TableIdentifierPB& table_identifier,
                                     bool is_index_table,
                                     bool update_indexed_table,
                                     std::vector<scoped_refptr<TableInfo>>* tables,
                                     std::vector<scoped_refptr<DeletedTableInfo>>* deleted_tables,
                                     std::vector<std::unique_ptr<TableInfo::lock_type>>* table_lcks,
                                     DeleteTableResponsePB* resp,
                                     rpc::RpcContext* rpc);

  // Request tablet servers to delete all replicas of the tablet.
  void DeleteTabletReplicas(const TabletInfo* tablet, const std::string& msg);

  // Marks each of the tablets in the given table as deleted and triggers requests
  // to the tablet servers to delete them.
  void DeleteTabletsAndSendRequests(const scoped_refptr<TableInfo>& table);

  // Send the "delete tablet request" to the specified TS/tablet.
  // The specified 'reason' will be logged on the TS.
  void SendDeleteTabletRequest(const TabletId& tablet_id,
                               tablet::TabletDataState delete_type,
                               const boost::optional<int64_t>& cas_config_opid_index_less_or_equal,
                               const scoped_refptr<TableInfo>& table,
                               TSDescriptor* ts_desc,
                               const std::string& reason);

  // Start a task to request the specified tablet leader to step down and optionally to remove
  // the server that is over-replicated. A new tablet server can be specified to start an election
  // immediately to become the new leader. If new_leader_ts_uuid is empty, the election will be run
  // following the protocol's default mechanism.
  void SendLeaderStepDownRequest(
      const scoped_refptr<TabletInfo>& tablet, const consensus::ConsensusStatePB& cstate,
      const string& change_config_ts_uuid, bool should_remove,
      const string& new_leader_ts_uuid = "");

  // Start a task to change the config to remove a certain voter because the specified tablet is
  // over-replicated.
  void SendRemoveServerRequest(
      const scoped_refptr<TabletInfo>& tablet, const consensus::ConsensusStatePB& cstate,
      const string& change_config_ts_uuid);

  // Start a task to change the config to add an additional voter because the
  // specified tablet is under-replicated.
  void SendAddServerRequest(
      const scoped_refptr<TabletInfo>& tablet, consensus::RaftPeerPB::MemberType member_type,
      const consensus::ConsensusStatePB& cstate, const string& change_config_ts_uuid);

  void GetPendingServerTasksUnlocked(const TableId &table_uuid,
                                     TabletToTabletServerMap *add_replica_tasks_map,
                                     TabletToTabletServerMap *remove_replica_tasks_map,
                                     TabletToTabletServerMap *stepdown_leader_tasks);

  std::string GenerateId() { return oid_generator_.Next(); }

  // Abort creation of 'table': abort all mutation for TabletInfo and
  // TableInfo objects (releasing all COW locks), abort all pending
  // tasks associated with the table, and erase any state related to
  // the table we failed to create from the in-memory maps
  // ('table_names_map_', 'table_ids_map_', 'tablet_map_' below).
  CHECKED_STATUS AbortTableCreation(TableInfo* table,
                                    const std::vector<TabletInfo*>& tablets,
                                    const Status& s,
                                    CreateTableResponsePB* resp);

  // Validates that the passed-in table replication information respects the overall cluster level
  // configuration. This should essentially not be more broader reaching than the cluster. As an
  // example, if the cluster is confined to AWS, you cannot have tables in GCE.
  CHECKED_STATUS ValidateTableReplicationInfo(const ReplicationInfoPB& replication_info);

  // Report metrics.
  void ReportMetrics();

  // Conventional "T xxx P yyy: " prefix for logging.
  std::string LogPrefix() const;

  // Aborts all tasks belonging to 'tables' and waits for them to finish.
  void AbortAndWaitForAllTasks(const std::vector<scoped_refptr<TableInfo>>& tables);

  // Can be used to create background_tasks_ field for this master.
  // Used on normal master startup or when master comes out of the shell mode.
  CHECKED_STATUS EnableBgTasks();

  // Set the current list of black listed nodes, which is used to track the load movement off of
  // these nodes. Also sets the initial load (which is the number of tablets on these nodes)
  // when the blacklist load removal operation was started. It permits overwrite semantics
  // for the blacklist.
  CHECKED_STATUS SetBlackList(const BlacklistPB& blacklist);

  // Given a tablet, find the leader uuid among its peers. If false is returned,
  // caller should not use the 'leader_uuid'.
  bool getLeaderUUID(const scoped_refptr<TabletInfo>& tablet,
                     TabletServerId* leader_uuid);

  // Calculate the total number of replicas which are being handled by blacklisted servers.
  int64_t GetNumBlacklistReplicas();

  int leader_ready_term() {
    std::lock_guard<simple_spinlock> l(state_lock_);
    return leader_ready_term_;
  }

  // Delete tables from internal map by id, if it has no more active tasks and tablets.
  void CleanUpDeletedTables();

  // Updated table state from DELETING to DELETED, if it has no more tablets.
  void MarkTableDeletedIfNoTablets(scoped_refptr<DeletedTableInfo> deleted_table,
                                   TableInfo* table_info = nullptr);

  // TODO: the maps are a little wasteful of RAM, since the TableInfo/TabletInfo
  // objects have a copy of the string key. But STL doesn't make it
  // easy to make a "gettable set".

  // Lock protecting the various in memory storage structures.
  typedef rw_spinlock LockType;
  mutable LockType lock_;

  TableInfoMap table_ids_map_;         // Table map: table-id -> TableInfo
  TableInfoByNameMap table_names_map_; // Table map: [namespace-id, table-name] -> TableInfo

  DeletedTabletMap deleted_tablet_map_; // Deleted Tablets map:
                                        // [tserver-id, tablet-id] -> DeletedTableInfo

  // Tablet maps: tablet-id -> TabletInfo
  TabletInfoMap tablet_map_;

  // Namespace maps: namespace-id -> NamespaceInfo and namespace-name -> NamespaceInfo
  typedef std::unordered_map<NamespaceName, scoped_refptr<NamespaceInfo> > NamespaceInfoMap;
  NamespaceInfoMap namespace_ids_map_;
  NamespaceInfoMap namespace_names_map_;

  // User-Defined type maps: udtype-id -> UDTypeInfo and udtype-name -> UDTypeInfo
  UDTypeInfoMap udtype_ids_map_;
  UDTypeInfoByNameMap udtype_names_map_;

  // Role map: RoleName -> RoleInfo
  typedef std::unordered_map<RoleName, scoped_refptr<RoleInfo> > RoleInfoMap;
  RoleInfoMap roles_map_;

  // TODO (Bristy) : Implement (resource) --> (role->permissions) map

  // Config information
  scoped_refptr<ClusterConfigInfo> cluster_config_ = nullptr;

  Master *master_;
  Atomic32 closing_;
  ObjectIdGenerator oid_generator_;

  // Random number generator used for selecting replica locations.
  ThreadSafeRandom rng_;

  gscoped_ptr<SysCatalogTable> sys_catalog_;

  // Mutex to avoid concurrent remote bootstrap sessions.
  std::mutex remote_bootstrap_mtx_;

  // Set to true if this master has received at least the superblock from a remote master.
  bool tablet_exists_;

  // Background thread, used to execute the catalog manager tasks
  // like the assignment and cleaner
  friend class CatalogManagerBgTasks;
  gscoped_ptr<CatalogManagerBgTasks> background_tasks_;

  // Track all information related to the black list operations.
  BlacklistState blacklistState;

  enum State {
    kConstructed,
    kStarting,
    kRunning,
    kClosing
  };

  // Lock protecting state_, leader_ready_term_
  mutable simple_spinlock state_lock_;
  State state_;

  // Used to defer work from reactor threads onto a thread where
  // blocking behavior is permissible.
  //
  // NOTE: Presently, this thread pool must contain only a single
  // thread (to correctly serialize invocations of ElectedAsLeaderCb
  // upon closely timed consecutive elections).
  gscoped_ptr<ThreadPool> worker_pool_;

  // This field is updated when a node becomes leader master,
  // waits for all outstanding uncommitted metadata (table and tablet metadata)
  // in the sys catalog to commit, and then reads that metadata into in-memory
  // data structures. This is used to "fence" client and tablet server requests
  // that depend on the in-memory state until this master can respond
  // correctly.
  int64_t leader_ready_term_;

  // Lock used to fence operations and leader elections. All logical operations
  // (i.e. create table, alter table, etc.) should acquire this lock for
  // reading. Following an election where this master is elected leader, it
  // should acquire this lock for writing before reloading the metadata.
  //
  // Readers should not acquire this lock directly; use ScopedLeadershipLock
  // instead.
  //
  // Always acquire this lock before state_lock_.
  RWMutex leader_lock_;

  // Async operations are accessing some private methods
  // (TODO: this stuff should be deferred and done in the background thread)
  friend class AsyncAlterTable;

  // Number of live tservers metric.
  scoped_refptr<AtomicGauge<uint32_t>> metric_num_tablet_servers_live_;

  friend class ClusterLoadBalancer;

  // Policy for load balancing tablets on tablet servers.
  std::unique_ptr<ClusterLoadBalancer> load_balance_policy_;

  // Tablet peer for the sys catalog tablet's peer.
  const scoped_refptr<tablet::TabletPeer> tablet_peer() const;

  // Use the Raft config that has been bootstrapped to update the in-memory state of master options
  // and also the on-disk state of the consensus meta object.
  CHECKED_STATUS UpdateMastersListInMemoryAndDisk();

  SystemTablesHandler sys_tables_handler_;

 private:
  DISALLOW_COPY_AND_ASSIGN(CatalogManager);
};

}  // namespace master
}  // namespace yb
#endif // YB_MASTER_CATALOG_MANAGER_H
