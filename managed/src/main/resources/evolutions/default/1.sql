# --- Created by Ebean DDL
# To stop Ebean DDL generation, remove this comment and start using Evolutions

# --- !Ups

create table access_key (
  key_code                      varchar(255) not null,
  provider_uuid                 uuid not null,
  key_info                      TEXT not null,
  constraint pk_access_key primary key (key_code,provider_uuid)
);

create table alert (
  uuid                          uuid not null,
  customer_uuid                 uuid not null,
  create_time                   timestamp not null,
  err_code                      Text not null,
  type                          varchar(255),
  message                       Text not null,
  constraint pk_alert primary key (uuid)
);

create table audit (
  id                            bigint not null,
  user_uuid                     uuid not null,
  customer_uuid                 uuid not null,
  payload                       TEXT,
  api_call                      TEXT not null,
  api_method                    TEXT not null,
  task_uuid                     uuid,
  timestamp                     timestamp not null,
  constraint uq_audit_task_uuid unique (task_uuid),
  constraint pk_audit primary key (id)
);
create sequence audit_id_seq increment by 1;

create table availability_zone (
  uuid                          uuid not null,
  code                          varchar(25) not null,
  name                          varchar(100) not null,
  region_uuid                   uuid,
  active                        boolean default true not null,
  subnet                        varchar(50),
  config                        TEXT,
  constraint pk_availability_zone primary key (uuid)
);

create table backup (
  backup_uuid                   uuid not null,
  customer_uuid                 uuid not null,
  state                         varchar(11) not null,
  backup_info                   TEXT not null,
  task_uuid                     uuid,
  create_time                   timestamp not null,
  update_time                   timestamp not null,
  constraint ck_backup_state check (state in ('Failed','Skipped','Completed','In Progress','Deleted')),
  constraint uq_backup_task_uuid unique (task_uuid),
  constraint pk_backup primary key (backup_uuid)
);

create table certificate_info (
  uuid                          uuid not null,
  customer_uuid                 uuid not null,
  label                         varchar(255),
  start_date                    timestamp not null,
  expiry_date                   timestamp not null,
  private_key                   varchar(255) not null,
  certificate                   varchar(255) not null,
  constraint uq_certificate_info_label unique (label),
  constraint pk_certificate_info primary key (uuid)
);

create table customer (
  id                            bigint not null,
  uuid                          uuid not null,
  code                          varchar(15) not null,
  name                          varchar(256) not null,
  creation_date                 timestamp not null,
  features                      TEXT,
  universe_uuids                TEXT not null,
  constraint uq_customer_uuid unique (uuid),
  constraint pk_customer primary key (id)
);
create sequence customer_id_seq increment by 1;

create table customer_config (
  config_uuid                   uuid not null,
  customer_uuid                 uuid not null,
  type                          varchar(25) not null,
  name                          varchar(100) not null,
  data                          TEXT not null,
  constraint ck_customer_config_type check (type in ('STORAGE','OTHER','CALLHOME','ALERTS')),
  constraint pk_customer_config primary key (config_uuid)
);

create table customer_task (
  id                            bigint not null,
  customer_uuid                 uuid not null,
  task_uuid                     uuid not null,
  target_type                   varchar(17) not null,
  target_name                   varchar(255) not null,
  type                          varchar(23) not null,
  target_uuid                   uuid not null,
  create_time                   timestamp not null,
  completion_time               timestamp,
  constraint ck_customer_task_target_type check (target_type in ('KMS Configuration','Table','Node','Backup','Universe','Cluster','Provider')),
  constraint ck_customer_task_type check (type in ('Delete','Add','DisableEncryptionAtRest','Stop','Start','Backup','UpgradeSoftware','Remove','SetEncryptionKey','Update','Restore','BulkImportData','EnableEncryptionAtRest','RotateEncryptionKey','Create','Release','UpgradeGflags')),
  constraint pk_customer_task primary key (id)
);
create sequence customer_task_id_seq increment by 1;

create table health_check (
  universe_uuid                 uuid not null,
  check_time                    timestamp not null,
  customer_id                   bigint,
  details_json                  TEXT not null,
  constraint pk_health_check primary key (universe_uuid,check_time)
);

create table instance_type (
  provider_code                 varchar(255) not null,
  instance_type_code            varchar(255) not null,
  active                        boolean default true not null,
  num_cores                     float not null,
  mem_size_gb                   float not null,
  instance_type_details_json    TEXT,
  constraint pk_instance_type primary key (provider_code,instance_type_code)
);

create table kms_config (
  config_uuid                   uuid not null,
  name                          varchar(100) not null,
  customer_uuid                 uuid not null,
  key_provider                  varchar(100) not null,
  auth_config                   TEXT not null,
  version                       integer not null,
  constraint ck_kms_config_key_provider check (key_provider in ('SMARTKEY','AWS')),
  constraint pk_kms_config primary key (config_uuid)
);

create table kms_history (
  key_ref                       varchar(255) not null,
  target_uuid                   uuid not null,
  type                          varchar(12) not null,
  timestamp                     timestamp not null,
  version                       integer not null,
  config_uuid                   uuid not null,
  active                        boolean not null,
  constraint ck_kms_history_type check (type in ('UNIVERSE_KEY')),
  constraint pk_kms_history primary key (key_ref,target_uuid,type)
);

create table metric_config (
  config_key                    varchar(100) not null,
  config                        TEXT not null,
  constraint pk_metric_config primary key (config_key)
);

create table node_instance (
  node_uuid                     uuid not null,
  instance_type_code            varchar(255),
  node_name                     varchar(255) not null,
  zone_uuid                     uuid not null,
  in_use                        boolean not null,
  node_details_json             varchar(255) not null,
  constraint pk_node_instance primary key (node_uuid)
);

create table price_component (
  provider_code                 varchar(255) not null,
  region_code                   varchar(255) not null,
  component_code                varchar(255) not null,
  price_details_json            TEXT not null,
  constraint pk_price_component primary key (provider_code,region_code,component_code)
);

create table provider (
  uuid                          uuid not null,
  code                          varchar(255) not null,
  name                          varchar(255) not null,
  active                        boolean default true not null,
  customer_uuid                 uuid not null,
  config                        TEXT not null,
  constraint pk_provider primary key (uuid)
);

create table region (
  uuid                          uuid not null,
  code                          varchar(25) not null,
  name                          varchar(100) not null,
  yb_image                      varchar(255),
  longitude                     float,
  latitude                      float,
  provider_uuid                 uuid,
  active                        boolean default true not null,
  details                       TEXT,
  config                        TEXT,
  constraint pk_region primary key (uuid)
);

create table schedule (
  schedule_uuid                 uuid not null,
  customer_uuid                 uuid not null,
  failure_count                 integer default 0 not null,
  frequency                     bigint not null,
  task_params                   TEXT not null,
  task_type                     varchar(29) not null,
  status                        varchar(7) not null,
  cron_expression               varchar(255),
  constraint ck_schedule_task_type check (task_type in ('CloudBootstrap','CloudCleanup','CreateCassandraTable','CreateUniverse','ReadOnlyClusterCreate','ReadOnlyClusterDelete','ReadOnlyKubernetesClusterDelete','CreateKubernetesUniverse','ReadOnlyKubernetesClusterCreate','DestroyUniverse','DestroyKubernetesUniverse','DeleteTable','BackupUniverse','MultiTableBackup','EditUniverse','EditKubernetesUniverse','KubernetesProvision','ImportIntoTable','UpgradeUniverse','UpgradeKubernetesUniverse','DeleteNodeFromUniverse','StopNodeInUniverse','StartNodeInUniverse','AddNodeToUniverse','RemoveNodeFromUniverse','ReleaseInstanceFromUniverse','SetUniverseKey','SetKubernetesUniverseKey','CreateKMSConfig','DeleteKMSConfig','AnsibleClusterServerCtl','AnsibleConfigureServers','AnsibleDestroyServer','AnsibleSetupServer','AnsibleUpdateNodeInfo','BulkImport','ChangeMasterConfig','CreateTable','DeleteNode','UpdateNodeProcess','DeleteTableFromUniverse','LoadBalancerStateChange','ModifyBlackList','ManipulateDnsRecordTask','RemoveUniverseEntry','SetNodeState','SwamperTargetsFileUpdate','UniverseUpdateSucceeded','UpdateAndPersistGFlags','UpdatePlacementInfo','UpdateSoftwareVersion','WaitForDataMove','WaitForLoadBalance','WaitForMasterLeader','WaitForServer','WaitForTServerHeartBeats','DeleteClusterFromUniverse','InstanceActions','WaitForServerReady','CloudAccessKeyCleanup','CloudAccessKeySetup','CloudInitializer','CloudProviderCleanup','CloudRegionCleanup','CloudRegionSetup','CloudSetup','BackupTable','WaitForLeadersOnPreferredOnly','EnableEncryptionAtRest','DisableEncryptionAtRest','DestroyEncryptionAtRest','KubernetesCommandExecutor','KubernetesWaitForPod','CopyEncryptionKeyFile','WaitForEncryptionKeyInMemory')),
  constraint ck_schedule_status check (status in ('Active','Stopped','Paused')),
  constraint pk_schedule primary key (schedule_uuid)
);

create table schedule_task (
  task_uuid                     uuid not null,
  schedule_uuid                 uuid not null,
  completed_time                timestamp,
  scheduled_time                timestamp,
  constraint pk_schedule_task primary key (task_uuid)
);

create table task_info (
  uuid                          uuid not null,
  parent_uuid                   uuid,
  position                      integer default -1,
  task_type                     varchar(29) not null,
  task_state                    varchar(12) not null,
  sub_task_group_type           varchar(25),
  percent_done                  integer default 0,
  details                       TEXT default '{}' not null,
  owner                         varchar(255) not null,
  create_time                   timestamp not null,
  update_time                   timestamp not null,
  constraint ck_task_info_task_type check (task_type in ('CloudBootstrap','CloudCleanup','CreateCassandraTable','CreateUniverse','ReadOnlyClusterCreate','ReadOnlyClusterDelete','ReadOnlyKubernetesClusterDelete','CreateKubernetesUniverse','ReadOnlyKubernetesClusterCreate','DestroyUniverse','DestroyKubernetesUniverse','DeleteTable','BackupUniverse','MultiTableBackup','EditUniverse','EditKubernetesUniverse','KubernetesProvision','ImportIntoTable','UpgradeUniverse','UpgradeKubernetesUniverse','DeleteNodeFromUniverse','StopNodeInUniverse','StartNodeInUniverse','AddNodeToUniverse','RemoveNodeFromUniverse','ReleaseInstanceFromUniverse','SetUniverseKey','SetKubernetesUniverseKey','CreateKMSConfig','DeleteKMSConfig','AnsibleClusterServerCtl','AnsibleConfigureServers','AnsibleDestroyServer','AnsibleSetupServer','AnsibleUpdateNodeInfo','BulkImport','ChangeMasterConfig','CreateTable','DeleteNode','UpdateNodeProcess','DeleteTableFromUniverse','LoadBalancerStateChange','ModifyBlackList','ManipulateDnsRecordTask','RemoveUniverseEntry','SetNodeState','SwamperTargetsFileUpdate','UniverseUpdateSucceeded','UpdateAndPersistGFlags','UpdatePlacementInfo','UpdateSoftwareVersion','WaitForDataMove','WaitForLoadBalance','WaitForMasterLeader','WaitForServer','WaitForTServerHeartBeats','DeleteClusterFromUniverse','InstanceActions','WaitForServerReady','CloudAccessKeyCleanup','CloudAccessKeySetup','CloudInitializer','CloudProviderCleanup','CloudRegionCleanup','CloudRegionSetup','CloudSetup','BackupTable','WaitForLeadersOnPreferredOnly','EnableEncryptionAtRest','DisableEncryptionAtRest','DestroyEncryptionAtRest','KubernetesCommandExecutor','KubernetesWaitForPod','CopyEncryptionKeyFile','WaitForEncryptionKeyInMemory')),
  constraint ck_task_info_task_state check (task_state in ('Unknown','Running','Success','Failure','Created','Initializing')),
  constraint ck_task_info_sub_task_group_type check (sub_task_group_type in ('Invalid','Provisioning','UpgradingSoftware','DownloadingSoftware','InstallingSoftware','ConfigureUniverse','WaitForDataMigration','RemovingUnusedServers','UpdatingGFlags','BootstrappingCloud','BootstrappingRegion','CreateAccessKey','InitializeCloudMetadata','CleanupCloud','CreatingTable','ImportingData','DeletingNode','StoppingNode','StartingNode','StartingNodeProcesses','StoppingNodeProcesses','AddingNode','RemovingNode','ReleasingInstance','DeletingTable','CreatingTableBackup','RestoringTableBackup','CreateNamespace','ApplySecret','HelmInit','HelmInstall','UpdateNumNodes','HelmDelete','KubernetesVolumeDelete','KubernetesNamespaceDelete','KubernetesPodInfo','KubernetesWaitForPod','HelmUpgrade','KubernetesUpgradePod','KubernetesInitYSQL')),
  constraint pk_task_info primary key (uuid)
);

create table universe (
  universe_uuid                 uuid not null,
  version                       integer not null,
  creation_date                 timestamp not null,
  name                          varchar(255),
  customer_id                   bigint,
  config                        TEXT,
  universe_details_json         TEXT not null,
  constraint uq_universe_name_customer_id unique (name,customer_id),
  constraint pk_universe primary key (universe_uuid)
);

create table users (
  uuid                          uuid not null,
  customer_uuid                 uuid not null,
  email                         varchar(256) not null,
  password_hash                 varchar(256) not null,
  creation_date                 timestamp not null,
  auth_token                    varchar(255),
  auth_token_issue_date         timestamp,
  api_token                     varchar(255),
  features                      TEXT,
  role                          varchar(10) not null,
  is_primary                    boolean not null,
  constraint ck_users_role check (role in ('ReadOnly','Admin','SuperAdmin')),
  constraint uq_users_email unique (email),
  constraint pk_users primary key (uuid)
);

create table yugaware_property (
  name                          varchar(255) not null,
  type                          varchar(6) not null,
  value                         TEXT,
  description                   TEXT,
  constraint ck_yugaware_property_type check (type in ('Config','System')),
  constraint pk_yugaware_property primary key (name)
);

alter table availability_zone add constraint fk_availability_zone_region_uuid foreign key (region_uuid) references region (uuid) on delete restrict on update restrict;
create index ix_availability_zone_region_uuid on availability_zone (region_uuid);

alter table region add constraint fk_region_provider_uuid foreign key (provider_uuid) references provider (uuid) on delete restrict on update restrict;
create index ix_region_provider_uuid on region (provider_uuid);


# --- !Downs

alter table availability_zone drop constraint if exists fk_availability_zone_region_uuid;
drop index if exists ix_availability_zone_region_uuid;

alter table region drop constraint if exists fk_region_provider_uuid;
drop index if exists ix_region_provider_uuid;

drop table if exists access_key;

drop table if exists alert;

drop table if exists audit;
drop sequence if exists audit_id_seq;

drop table if exists availability_zone;

drop table if exists backup;

drop table if exists certificate_info;

drop table if exists customer;
drop sequence if exists customer_id_seq;

drop table if exists customer_config;

drop table if exists customer_task;
drop sequence if exists customer_task_id_seq;

drop table if exists health_check;

drop table if exists instance_type;

drop table if exists kms_config;

drop table if exists kms_history;

drop table if exists metric_config;

drop table if exists node_instance;

drop table if exists price_component;

drop table if exists provider;

drop table if exists region;

drop table if exists schedule;

drop table if exists schedule_task;

drop table if exists task_info;

drop table if exists universe;

drop table if exists users;

drop table if exists yugaware_property;

