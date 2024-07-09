/**
 * Do not edit manually.
 *
 * YugabyteDB Anywhere V2 APIs
 * An improved set of APIs for managing YugabyteDB Anywhere
 * OpenAPI spec version: v2
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 *
 */
export type DeleteClusterParams = { isForceDelete?: boolean };

export type UniverseRestartReqBody = UniverseRestart;

export type UniverseSoftwareUpgradePrecheckReqBody = UniverseSoftwareUpgradePrecheckReq;

export type UniverseRollbackUpgradeReqBody = UniverseRollbackUpgradeReq;

export type UniverseThirdPartySoftwareUpgradeReqBody = UniverseThirdPartySoftwareUpgradeStart;

export type UniverseSoftwareUpgradeFinalizeBody = UniverseSoftwareUpgradeFinalize;

export type UniverseSoftwareUpgradeReqBody = UniverseSoftwareUpgradeStart;

export type UniverseEditGFlagsReqBody = UniverseEditGFlags;

export type ClusterAddReqBody = ClusterAddSpec;

export type UniverseCreateReqBody = UniverseCreateSpec;

export type UniverseDeleteReqBody = UniverseDeleteSpec;

export type UniverseEditReqBody = UniverseEditSpec;

/**
 * Software Upgrade Precheck success
 */
export type UniverseSoftwareUpgradePrecheckResponseResponse = UniverseSoftwareUpgradePrecheckResp;

/**
 * Software Upgrade Finalize information
 */
export type UniverseSoftwareUpgradeFinalizeRespResponse = UniverseSoftwareUpgradeFinalizeInfo;

/**
 * task accepted
 */
export type YBATaskRespResponse = YBATask;

/**
 * success
 */
export type UniverseRespResponse = Universe;

/**
 * The method to reboot the node. This is not required for kubernetes universes, as the pods 
will get restarted no matter what. "HARD" reboots are not supported today.

OS: Restarts the node via the operating system.
SERVICE: Restart the YugabyteDB Process only (master, tserver, etc).

 */
export type UniverseRestartAllOfRestartType = 'OS' | 'SERVICE';

// eslint-disable-next-line @typescript-eslint/no-redeclare
export const UniverseRestartAllOfRestartType = {
  OS: 'OS' as UniverseRestartAllOfRestartType,
  SERVICE: 'SERVICE' as UniverseRestartAllOfRestartType
};

export type UniverseRestartAllOf = {
  /** Perform a rolling restart of the universe. Otherwise, all nodes will be restarted at the same time. */
  rolling_restart?: boolean;
  /** The method to reboot the node. This is not required for kubernetes universes, as the pods 
will get restarted no matter what. "HARD" reboots are not supported today.

OS: Restarts the node via the operating system.
SERVICE: Restart the YugabyteDB Process only (master, tserver, etc).
 */
  restart_type?: UniverseRestartAllOfRestartType;
};

/**
 * UniverseRestart

Payload to restart a Universe. Part of UniverseRestartReq. This will restart all nodes in the 
universe or just restart the master and tserver processes.

 */
export type UniverseRestart = SleepAfterRestartSchema & UniverseRestartAllOf;

/**
 * UniverseSoftwareUpgradePrecheckResp

Response to a YugabyteDB software upgrade precheck on a Universe. Returns if a finalize is 
required. Part of UniverseSoftwareUpgradePrecheckResponse.

 */
export interface UniverseSoftwareUpgradePrecheckResp {
  /** If the upgrade requires a finalize step. If true, the user must call the finalize 
endpoint to complete the upgrade.
 */
  finalize_required: boolean;
}

/**
 * UniverseSoftwareUpgradePrecheckReq

Payload to precheck a YugabyteDB software upgrade on a Universe. Part of 
UniverseSoftwareUpgradePrecheckReq

 */
export interface UniverseSoftwareUpgradePrecheckReq {
  /** Run prechecks as if the universe would be upgraded to this version. */
  yb_software_version: string;
}

/**
 * UniverseRollbackUpgrade

Payload to rollback a YugabyteDB software upgrade on a Universe. Part of 
UniverseRollbackUpgradeReq

 */
export type UniverseRollbackUpgradeReq = SleepAfterRestartSchema & UniverseUpgradeOptionRolling;

export type UniverseThirdPartySoftwareUpgradeStartAllOf = {
  /** force all thirdparty softwares to be upgraded. */
  force_all?: boolean;
};

/**
 * UniverseThirdPartyUpgradeStart

Payload to start a third party software upgrade on a Universe. Part of 
UniverseThirdPartyUpgradeReq

 */
export type UniverseThirdPartySoftwareUpgradeStart = SleepAfterRestartSchema &
  UniverseThirdPartySoftwareUpgradeStartAllOf;

/**
 * UniverseSoftwareUpgradeFinalize

Payload to finalize a YugabyteDB software upgrade on a Universe

 */
export interface UniverseSoftwareUpgradeFinalize {
  /** Upgrade the YSQL System Catalog. */
  upgrade_system_catalog?: boolean;
}

/**
 * ImpactedXClusterInfo

XCluster info that are impacted by software upgrade. Part of SoftwareUpgradesFinalize schema.

 */
export interface UniverseSoftwareFinalizeImpactedXCluster {
  /** UUID of the Universe */
  universe_uuid: string;
  /** Name of the Universe */
  universe_name: string;
  /** Version of the Universe */
  universe_version: string;
}

/**
 * SoftwareUpgradesFinalizeInfo

The list of connected xclusters that will be impacted by finalizing the software upgrade. Part of
SoftwareUpgradeFinalizeResponse.

 */
export interface UniverseSoftwareUpgradeFinalizeInfo {
  /** List of XCluster info that are impacted by software upgrade */
  impacted_xclusters?: UniverseSoftwareFinalizeImpactedXCluster[];
}

export type UniverseSoftwareUpgradeStartAllOf = {
  /** perform an upgrade where rollback is allowed */
  allow_rollback?: boolean;
  /** Upgrade the YugabyteDB Catalog */
  upgrade_system_catalog?: boolean;
  /** The target release version to upgrade to. */
  version: string;
};

/**
 * UniverseUpgradeOptionRolling
Option for an upgrade to be rolling (one node at a time) or non-rolling (all nodes at once, with downtime)
 */
export interface UniverseUpgradeOptionRolling {
  /** Perform a rolling upgrade where only one node is upgraded at a time. This is the default
behavior. False will perform a non-rolling upgrade where all nodes are upgraded at the same
 */
  rolling_upgrade?: boolean;
}

/**
 * UniverseSoftwareUpgradeStart

Payload to start a YugabyteDB software upgrade on a Universe. Part of UniverseSoftwareUpgradeReq

 */
export type UniverseSoftwareUpgradeStart = SleepAfterRestartSchema &
  UniverseUpgradeOptionRolling &
  UniverseSoftwareUpgradeStartAllOf;

/**
 * GFlags for each cluster uuid of this universe
 */
export type UniverseEditGFlagsAllOfUniverseGflags = { [key: string]: ClusterGFlags };

/**
 * Type of YBA resource to operate upon
 */
export type KubernetesResourceDetailsResourceType = 'UNIVERSE';

// eslint-disable-next-line @typescript-eslint/no-redeclare
export const KubernetesResourceDetailsResourceType = {
  UNIVERSE: 'UNIVERSE' as KubernetesResourceDetailsResourceType
};

/**
 * Identifies the K8S Resource to operate upon. An internal object used as part of Universe edit operations.
 */
export interface KubernetesResourceDetails {
  /** Type of YBA resource to operate upon */
  resource_type?: KubernetesResourceDetailsResourceType;
  /** Name of this Kubernetes resource */
  name?: string;
  /** Kubernetes namespace in which the resource is found */
  namespace?: string;
}

export type UniverseEditGFlagsAllOf = {
  kubernetes_resource_details?: KubernetesResourceDetails;
  /** GFlags for each cluster uuid of this universe */
  universe_gflags?: UniverseEditGFlagsAllOfUniverseGflags;
};

/**
 * Option for an upgrade to be rolling (one node at a time) or non-rolling (all nodes at once, with
downtime)

 */
export type UniverseUpgradeOptionsAllUpgradeOption = 'Rolling' | 'Non-rolling' | 'Non-restart';

// eslint-disable-next-line @typescript-eslint/no-redeclare
export const UniverseUpgradeOptionsAllUpgradeOption = {
  Rolling: 'Rolling' as UniverseUpgradeOptionsAllUpgradeOption,
  Nonrolling: 'Non-rolling' as UniverseUpgradeOptionsAllUpgradeOption,
  Nonrestart: 'Non-restart' as UniverseUpgradeOptionsAllUpgradeOption
};

/**
 * UniverseUpgradeOptionsAll

Option on how to handle node reboots:
  Rolling     - Apply upgrade to each node one at a time (Default)
  Non-rolling - Apply upgrade to all nodes at the same time, so has client downtime
  Non-restart - Apply upgrade without restarting nodes or processes. 
                Applicable to only certain supported Upgrades and GFlags.

 */
export interface UniverseUpgradeOptionsAll {
  /** Option for an upgrade to be rolling (one node at a time) or non-rolling (all nodes at once, with
downtime)
 */
  upgrade_option?: UniverseUpgradeOptionsAllUpgradeOption;
}

/**
 * Time to wait after restarting a tserver or master process
 */
export interface SleepAfterRestartSchema {
  /** Applicable for rolling restarts. Time to wait between master restarts. Defaults to 180000. */
  sleep_after_master_restart_millis?: number;
  /** Applicable for rolling restarts. Time to wait between tserver restarts. Defaults to 180000. */
  sleep_after_tserver_restart_millis?: number;
}

/**
 * UniverseEditGFlags

Request payload to edit GFlags of a Universe.

 */
export type UniverseEditGFlags = SleepAfterRestartSchema &
  UniverseUpgradeOptionsAll &
  UniverseEditGFlagsAllOf;

/**
 * A map of strings representing a set of Tags and Values to apply on nodes in the aws/gcp/azu cloud. See https://docs.yugabyte.com/preview/yugabyte-platform/manage-deployments/instance-tags/.
 */
export type ClusterAddSpecInstanceTags = { [key: string]: string };

/**
 * Cluster type can be one of READ_REPLICA, ADDON
 */
export type ClusterAddSpecClusterType = 'READ_REPLICA' | 'ADDON';

// eslint-disable-next-line @typescript-eslint/no-redeclare
export const ClusterAddSpecClusterType = {
  READ_REPLICA: 'READ_REPLICA' as ClusterAddSpecClusterType,
  ADDON: 'ADDON' as ClusterAddSpecClusterType
};

/**
 * CPU Arch of DB nodes.
 */
export type UniverseCreateSpecArch = 'x86_64' | 'aarch64';

// eslint-disable-next-line @typescript-eslint/no-redeclare
export const UniverseCreateSpecArch = {
  x86_64: 'x86_64' as UniverseCreateSpecArch,
  aarch64: 'aarch64' as UniverseCreateSpecArch
};

/**
 * UniverseCreateSpec

Universe create time properties. This is used to create a new Universe.

 */
export interface UniverseCreateSpec {
  spec: UniverseSpec;
  /** CPU Arch of DB nodes. */
  arch: UniverseCreateSpecArch;
}

/**
 * UniverseDeleteSpec

Optional addtional parameters for deleting a universe.

 */
export interface UniverseDeleteSpec {
  /** Whether to force delete the universe */
  is_force_delete?: boolean;
  /** Whether to delete backups associated with the universe */
  is_delete_backups?: boolean;
  /** Whether to delete associated Encryption In Transit certificates */
  is_delete_associated_certs?: boolean;
}

/**
 * YBATask

YugabyteDB Anywhere Task object is used to represent a task that is being executed on a resource. This Task object can be polled for progress and status of the task. This object is returned as a response to most of the YugabyteDB Anywhere API calls.

 */
export interface YBATask {
  /** UUID of the resource being modified by the task */
  readonly resource_uuid?: string;
  /** Task UUID */
  readonly task_uuid?: string;
}

/**
 * A map of strings representing a set of Tags and Values to apply on nodes in the aws/gcp/azu cloud. See https://docs.yugabyte.com/preview/yugabyte-platform/manage-deployments/instance-tags/.
 */
export type ClusterEditSpecInstanceTags = { [key: string]: string };

/**
 * Edit Cloud Provider settings for the cluster. Part of ClusterAddSpec and ClusterEditSpec.
 */
export interface ClusterProviderEditSpec {
  /** Edit the list of regions in the cloud provider to place data replicas */
  region_list: string[];
}

/**
 * ClusterAddSpec

Request payload to add a new cluster to an existing YugabyteDB Universe.

 */
export interface ClusterAddSpec {
  /** Cluster type can be one of READ_REPLICA, ADDON */
  cluster_type: ClusterAddSpecClusterType;
  /** Set the number of nodes (tservers) to provision in this cluster */
  num_nodes: number;
  node_spec: ClusterNodeSpec;
  provider_spec: ClusterProviderEditSpec;
  placement_spec?: ClusterPlacementSpec;
  /** A map of strings representing a set of Tags and Values to apply on nodes in the aws/gcp/azu cloud. See https://docs.yugabyte.com/preview/yugabyte-platform/manage-deployments/instance-tags/. */
  instance_tags?: ClusterAddSpecInstanceTags;
  gflags?: ClusterGFlags;
}

/**
 * Edit Cluster Spec. Part of UniverseEditSpec.
 */
export interface ClusterEditSpec {
  /** The system generated cluster uuid to edit. This can be fetched from ClusterInfo. */
  uuid: string;
  /** Set the number of nodes (tservers) to provision in this cluster */
  num_nodes?: number;
  node_spec?: ClusterNodeSpec;
  provider_spec?: ClusterProviderEditSpec;
  placement_spec?: ClusterPlacementSpec;
  /** A map of strings representing a set of Tags and Values to apply on nodes in the aws/gcp/azu cloud. See https://docs.yugabyte.com/preview/yugabyte-platform/manage-deployments/instance-tags/. */
  instance_tags?: ClusterEditSpecInstanceTags;
}

/**
 * UniverseEditSpec

Request payload to edit an existing Universe.
Not all properties of a UniverseSpec can be updated.
This is a partial update. Only the fields that are provided will be updated.

 */
export interface UniverseEditSpec {
  clusters: ClusterEditSpec[];
  /** Expected universe version. Set to -1 to ignore version checking. */
  expected_universe_version: number;
}

/**
 * Universe

Contains the user provided `spec` and system generated `info` of a Universe. Returned as response payload for a GET request on a Universe.

 */
export interface Universe {
  spec?: UniverseSpec;
  info?: UniverseInfo;
}

/**
 * The state of the last YugabyteDB software upgrade operation on this universe
 */
export type UniverseInfoSoftwareUpgradeState =
  | 'Ready'
  | 'Upgrading'
  | 'UpgradeFailed'
  | 'PreFinalize'
  | 'Finalizing'
  | 'FinalizeFailed'
  | 'RollingBack'
  | 'RollbackFailed';

// eslint-disable-next-line @typescript-eslint/no-redeclare
export const UniverseInfoSoftwareUpgradeState = {
  Ready: 'Ready' as UniverseInfoSoftwareUpgradeState,
  Upgrading: 'Upgrading' as UniverseInfoSoftwareUpgradeState,
  UpgradeFailed: 'UpgradeFailed' as UniverseInfoSoftwareUpgradeState,
  PreFinalize: 'PreFinalize' as UniverseInfoSoftwareUpgradeState,
  Finalizing: 'Finalizing' as UniverseInfoSoftwareUpgradeState,
  FinalizeFailed: 'FinalizeFailed' as UniverseInfoSoftwareUpgradeState,
  RollingBack: 'RollingBack' as UniverseInfoSoftwareUpgradeState,
  RollbackFailed: 'RollbackFailed' as UniverseInfoSoftwareUpgradeState
};

/**
 * CPU Arch of DB nodes.
 */
export type UniverseInfoArch = 'x86_64' | 'aarch64';

// eslint-disable-next-line @typescript-eslint/no-redeclare
export const UniverseInfoArch = {
  x86_64: 'x86_64' as UniverseInfoArch,
  aarch64: 'aarch64' as UniverseInfoArch
};

/**
 * Details of a cloud node. Part of UniverseInfo.
 */
export interface NodeDetails {
  /** The availability zone's UUID */
  az_uuid?: string;
  /** Node information, as reported by the cloud provider */
  cloud_info?: CloudSpecificInfo;
  /** True if cron jobs were properly configured for this node */
  crons_active?: boolean;
  /** Used for configurations where each node can have only one process */
  dedicated_to?: NodeDetailsDedicatedTo;
  /** Disks are mounted by uuid */
  disks_are_mounted_by_uuid?: boolean;
  /** True if this node is a master */
  is_master?: boolean;
  /** True if this node is a REDIS server */
  is_redis_server?: boolean;
  /** True if this node is a Tablet server */
  is_tserver?: boolean;
  /** True if this node is a YCQL server */
  is_yql_server?: boolean;
  /** True if this node is a YSQL server */
  is_ysql_server?: boolean;
  /** Store last volume update time */
  readonly last_volume_update_time?: string;
  /** Machine image name */
  machine_image?: string;
  /** Master state */
  master_state?: NodeDetailsMasterState;
  /** Node ID */
  node_idx?: number;
  /** Node name */
  node_name?: string;
  /** Node UUID */
  node_uuid?: string;
  /** UUID of the cluster to which this node belongs */
  placement_uuid?: string;
  /** SSH port override for the AMI */
  ssh_port_override?: number;
  /** SSH user override for the AMI */
  ssh_user_override?: string;
  /** Node state */
  state?: NodeDetailsState;
  /** True if this a custom YB AMI */
  yb_prebuilt_ami?: boolean;
}

/**
 * These are read-only system generated properties of a Universe. Returned as part of a Universe resource.
 */
export interface UniverseInfo {
  /** UUID of the Universe */
  readonly universe_uuid?: string;
  /** Universe version */
  readonly version?: number;
  /** Universe creation date */
  readonly creation_date?: string;
  /** CPU Arch of DB nodes. */
  readonly arch?: UniverseInfoArch;
  /** DNS name */
  readonly dns_name?: string;
  /** YBC Software version installed in DB nodes of this Universe */
  readonly ybc_software_version?: string;
  /** YBA that is managing this Universe */
  readonly platform_url?: string;
  /** A globally unique name generated as a combination of the customer id and the universe name. This is used as the prefix of node names in the universe. Can be configured at the time of universe creation. */
  readonly node_prefix?: string;
  encryption_at_rest_info?: EncryptionAtRestInfo;
  /** Whether a create/edit/destroy intent on the universe is currently running. */
  readonly update_in_progress?: boolean;
  /** Type of task which set updateInProgress flag. */
  readonly updating_task?: string;
  /** UUID of task which set updateInProgress flag. */
  readonly updating_task_uuid?: string;
  /** Whether the latest operation on this universe has successfully completed. Is updated for each operation on the universe. */
  readonly update_succeeded?: boolean;
  /** Whether the universe is in the paused state */
  readonly universe_paused?: boolean;
  /** UUID of last failed task that applied modification to cluster state */
  readonly placement_modification_task_uuid?: string;
  /** The state of the last YugabyteDB software upgrade operation on this universe */
  readonly software_upgrade_state?: UniverseInfoSoftwareUpgradeState;
  /** Whether a rollback of the last YugabyteDB upgrade operation is allowed */
  readonly is_software_rollback_allowed?: boolean;
  /** Set to true if nodes of this Universe can be resized without a full move */
  readonly nodes_resize_available?: boolean;
  /** Whether this Universe is created and controlled by the Kubernetes Operator */
  readonly is_kubernetes_operator_controlled?: boolean;
  /** Whether OpenTelemetry Collector is enabled for universe */
  otel_collector_enabled?: boolean;
  x_cluster_info?: XClusterInfo;
  clusters?: ClusterInfo[];
  /** Node details */
  node_details_set?: NodeDetails[];
}

/**
 * Node state
 */
export type NodeDetailsState =
  | 'ToBeAdded'
  | 'InstanceCreated'
  | 'ServerSetup'
  | 'ToJoinCluster'
  | 'Reprovisioning'
  | 'Provisioned'
  | 'SoftwareInstalled'
  | 'UpgradeSoftware'
  | 'RollbackUpgrade'
  | 'FinalizeUpgrade'
  | 'UpdateGFlags'
  | 'Live'
  | 'Stopping'
  | 'Starting'
  | 'Stopped'
  | 'Unreachable'
  | 'MetricsUnavailable'
  | 'ToBeRemoved'
  | 'Removing'
  | 'Removed'
  | 'Adding'
  | 'BeingDecommissioned'
  | 'Decommissioned'
  | 'UpdateCert'
  | 'ToggleTls'
  | 'ConfigureDBApis'
  | 'Resizing'
  | 'SystemdUpgrade'
  | 'Terminating'
  | 'Terminated'
  | 'Rebooting'
  | 'HardRebooting'
  | 'VMImageUpgrade';

// eslint-disable-next-line @typescript-eslint/no-redeclare
export const NodeDetailsState = {
  ToBeAdded: 'ToBeAdded' as NodeDetailsState,
  InstanceCreated: 'InstanceCreated' as NodeDetailsState,
  ServerSetup: 'ServerSetup' as NodeDetailsState,
  ToJoinCluster: 'ToJoinCluster' as NodeDetailsState,
  Reprovisioning: 'Reprovisioning' as NodeDetailsState,
  Provisioned: 'Provisioned' as NodeDetailsState,
  SoftwareInstalled: 'SoftwareInstalled' as NodeDetailsState,
  UpgradeSoftware: 'UpgradeSoftware' as NodeDetailsState,
  RollbackUpgrade: 'RollbackUpgrade' as NodeDetailsState,
  FinalizeUpgrade: 'FinalizeUpgrade' as NodeDetailsState,
  UpdateGFlags: 'UpdateGFlags' as NodeDetailsState,
  Live: 'Live' as NodeDetailsState,
  Stopping: 'Stopping' as NodeDetailsState,
  Starting: 'Starting' as NodeDetailsState,
  Stopped: 'Stopped' as NodeDetailsState,
  Unreachable: 'Unreachable' as NodeDetailsState,
  MetricsUnavailable: 'MetricsUnavailable' as NodeDetailsState,
  ToBeRemoved: 'ToBeRemoved' as NodeDetailsState,
  Removing: 'Removing' as NodeDetailsState,
  Removed: 'Removed' as NodeDetailsState,
  Adding: 'Adding' as NodeDetailsState,
  BeingDecommissioned: 'BeingDecommissioned' as NodeDetailsState,
  Decommissioned: 'Decommissioned' as NodeDetailsState,
  UpdateCert: 'UpdateCert' as NodeDetailsState,
  ToggleTls: 'ToggleTls' as NodeDetailsState,
  ConfigureDBApis: 'ConfigureDBApis' as NodeDetailsState,
  Resizing: 'Resizing' as NodeDetailsState,
  SystemdUpgrade: 'SystemdUpgrade' as NodeDetailsState,
  Terminating: 'Terminating' as NodeDetailsState,
  Terminated: 'Terminated' as NodeDetailsState,
  Rebooting: 'Rebooting' as NodeDetailsState,
  HardRebooting: 'HardRebooting' as NodeDetailsState,
  VMImageUpgrade: 'VMImageUpgrade' as NodeDetailsState
};

/**
 * Master state
 */
export type NodeDetailsMasterState = 'None' | 'ToStart' | 'Configured' | 'ToStop';

// eslint-disable-next-line @typescript-eslint/no-redeclare
export const NodeDetailsMasterState = {
  None: 'None' as NodeDetailsMasterState,
  ToStart: 'ToStart' as NodeDetailsMasterState,
  Configured: 'Configured' as NodeDetailsMasterState,
  ToStop: 'ToStop' as NodeDetailsMasterState
};

/**
 * Used for configurations where each node can have only one process
 */
export type NodeDetailsDedicatedTo =
  | 'MASTER'
  | 'TSERVER'
  | 'CONTROLLER'
  | 'YQLSERVER'
  | 'YSQLSERVER'
  | 'REDISSERVER'
  | 'EITHER';

// eslint-disable-next-line @typescript-eslint/no-redeclare
export const NodeDetailsDedicatedTo = {
  MASTER: 'MASTER' as NodeDetailsDedicatedTo,
  TSERVER: 'TSERVER' as NodeDetailsDedicatedTo,
  CONTROLLER: 'CONTROLLER' as NodeDetailsDedicatedTo,
  YQLSERVER: 'YQLSERVER' as NodeDetailsDedicatedTo,
  YSQLSERVER: 'YSQLSERVER' as NodeDetailsDedicatedTo,
  REDISSERVER: 'REDISSERVER' as NodeDetailsDedicatedTo,
  EITHER: 'EITHER' as NodeDetailsDedicatedTo
};

/**
 * Node information reported by the Cloud Provider. Part of NodeDetails.
 */
export interface CloudSpecificInfo {
  /** True if the node has a public IP address assigned */
  assign_public_ip?: boolean;
  /** The node's availability zone */
  az?: string;
  /** The node's Cloud Provider */
  cloud?: string;
  /** The node's instance type */
  instance_type?: string;
  /** Kubernetes namespace */
  kubernetes_namespace?: string;
  /** Pod name in Kubernetes */
  kubernetes_pod_name?: string;
  /** Mounted disks LUN indexes */
  lun_indexes?: number[];
  /** Mount roots */
  mount_roots?: string;
  /** The node's private DNS */
  private_dns?: string;
  /** The node's private IP address */
  private_ip?: string;
  /** The node's public DNS name */
  public_dns?: string;
  /** The node's public IP address */
  public_ip?: string;
  /** The node's region */
  region?: string;
  /** Root volume ID or name */
  root_volume?: string;
  /** Secondary Private IP */
  secondary_private_ip?: string;
  /** Secondary Subnet IP */
  secondary_subnet_id?: string;
  /** ID of the subnet on which this node is deployed */
  subnet_id?: string;
  /** True if `use time sync` is enabled */
  use_time_sync?: boolean;
}

/**
 * Universe Cluster Info. Part of the Response payload UniverseInfo that describes system generated properties of a Cluster.
 */
export interface ClusterInfo {
  /** cluster uuid */
  uuid?: string;
  /** TBD */
  spot_price?: number;
}

/**
 * XCluster related states in this universe. Part of UniverseInfo.
 */
export interface XClusterInfo {
  /** The value of certs_for_cdc_dir gflag */
  source_root_cert_dir_path?: string;
  /** The source universe's xcluster replication relationships */
  source_x_cluster_configs?: string[];
  /** The target universe's xcluster replication relationships */
  target_x_cluster_configs?: string[];
}

/**
 * Encryption At Rest specification for the Universe. Part of UniverseInfo.
 */
export interface EncryptionAtRestInfo {
  /** Whether a Universe is currently encrypted at rest */
  readonly encryption_at_rest_status?: boolean;
}

/**
 * These are user configured properties of a Universe. Part of create Universe request payload. Returned as part of a Universe resource.
 */
export interface UniverseSpec {
  /** Name of the Universe */
  name: string;
  /** The YugabyteDB software version to install. This can be upgraded using API /customers/:cUUID/universes/:uniUUID/upgrade/software */
  yb_software_version: string;
  encryption_at_rest_spec?: EncryptionAtRestSpec;
  encryption_in_transit_spec?: EncryptionInTransitSpec;
  ysql?: YSQLSpec;
  ycql?: YCQLSpec;
  /** Whether to use time sync services like chrony on DB nodes of this cluster */
  use_time_sync?: boolean;
  /** Whether to enable systemd on nodes of cluster. Defaults to false. Can be changed using API /customers/:cUUID/universes/:uniUUID/upgrade/systemd. */
  use_systemd?: boolean;
  /** Path to download thirdparty packages for itest. Only for AWS/onprem. */
  remote_package_path?: string;
  /** Override the default DB present in pre-built Ami. YBM usage. */
  override_prebuilt_ami_db_version?: boolean;
  networking_spec?: UniverseNetworkingSpec;
  clusters: ClusterSpec[];
}

/**
 * A map of strings representing a set of Tags and Values to apply on nodes in the aws/gcp/azu cloud. See https://docs.yugabyte.com/preview/yugabyte-platform/manage-deployments/instance-tags/.
 */
export type ClusterSpecInstanceTags = { [key: string]: string };

/**
 * Cluster type can be one of PRIMARY, ASYNC (for ReadOnly), ADDON
 */
export type ClusterSpecClusterType = 'PRIMARY' | 'ASYNC' | 'ADDON';

// eslint-disable-next-line @typescript-eslint/no-redeclare
export const ClusterSpecClusterType = {
  PRIMARY: 'PRIMARY' as ClusterSpecClusterType,
  ASYNC: 'ASYNC' as ClusterSpecClusterType,
  ADDON: 'ADDON' as ClusterSpecClusterType
};

export type ClusterGFlagsAllOf = {
  /** GFlags per availability zone uuid */
  az_gflags?: ClusterGFlagsAllOfAzGflags;
};

/**
 * GFlags for a single cluster of a YugabyteDB Universe. Used as part of ClusterSpec at Universe create time, and as part of UniverseEditGFlags to edit GFlags for a Universe.
 */
export type ClusterGFlags = AvailabilityZoneGFlags & ClusterGFlagsAllOf;

/**
 * GFlags applied on Master process
 */
export type AvailabilityZoneGFlagsMaster = { [key: string]: string };

/**
 * GFlags applied on TServer process
 */
export type AvailabilityZoneGFlagsTserver = { [key: string]: string };

/**
 * GFlags for tserver and master. Part of ClusterGFlags.
 */
export interface AvailabilityZoneGFlags {
  /** GFlags applied on TServer process */
  tserver?: AvailabilityZoneGFlagsTserver;
  /** GFlags applied on Master process */
  master?: AvailabilityZoneGFlagsMaster;
}

/**
 * GFlags per availability zone uuid
 */
export type ClusterGFlagsAllOfAzGflags = { [key: string]: AvailabilityZoneGFlags };

/**
 * Log level
 */
export type YSQLAuditConfigLogLevel =
  | 'DEBUG1'
  | 'DEBUG2'
  | 'DEBUG3'
  | 'DEBUG4'
  | 'DEBUG5'
  | 'INFO'
  | 'NOTICE'
  | 'WARNING'
  | 'LOG';

// eslint-disable-next-line @typescript-eslint/no-redeclare
export const YSQLAuditConfigLogLevel = {
  DEBUG1: 'DEBUG1' as YSQLAuditConfigLogLevel,
  DEBUG2: 'DEBUG2' as YSQLAuditConfigLogLevel,
  DEBUG3: 'DEBUG3' as YSQLAuditConfigLogLevel,
  DEBUG4: 'DEBUG4' as YSQLAuditConfigLogLevel,
  DEBUG5: 'DEBUG5' as YSQLAuditConfigLogLevel,
  INFO: 'INFO' as YSQLAuditConfigLogLevel,
  NOTICE: 'NOTICE' as YSQLAuditConfigLogLevel,
  WARNING: 'WARNING' as YSQLAuditConfigLogLevel,
  LOG: 'LOG' as YSQLAuditConfigLogLevel
};

export type YSQLAuditConfigClassesItem =
  | 'READ'
  | 'WRITE'
  | 'FUNCTION'
  | 'ROLE'
  | 'DDL'
  | 'MISC'
  | 'MISC_SET';

// eslint-disable-next-line @typescript-eslint/no-redeclare
export const YSQLAuditConfigClassesItem = {
  READ: 'READ' as YSQLAuditConfigClassesItem,
  WRITE: 'WRITE' as YSQLAuditConfigClassesItem,
  FUNCTION: 'FUNCTION' as YSQLAuditConfigClassesItem,
  ROLE: 'ROLE' as YSQLAuditConfigClassesItem,
  DDL: 'DDL' as YSQLAuditConfigClassesItem,
  MISC: 'MISC' as YSQLAuditConfigClassesItem,
  MISC_SET: 'MISC_SET' as YSQLAuditConfigClassesItem
};

/**
 * YSQL Audit Logging Configuration. Part of AuditLogConfig.
 */
export interface YSQLAuditConfig {
  /** YSQL statement classes */
  classes: YSQLAuditConfigClassesItem[];
  /** Enabled */
  readonly enabled: boolean;
  /** Log catalog */
  log_catalog: boolean;
  /** Log client */
  log_client: boolean;
  /** Log level */
  log_level: YSQLAuditConfigLogLevel;
  /** Log parameter */
  log_parameter: boolean;
  /** Log parameter max size */
  log_parameter_max_size: number;
  /** Log relation */
  log_relation: boolean;
  /** Log row */
  log_rows: boolean;
  /** Log statement */
  log_statement: boolean;
  /** Log statement once */
  log_statement_once: boolean;
}

/**
 * Audit Log Configuration Specification for the Universe. Part of Clusterspec.
 */
export interface AuditLogConfig {
  /** Universe logs export active */
  export_active?: boolean;
  /** Universe logs exporter config */
  universe_logs_exporter_config: UniverseLogsExporterConfig[];
  /** YCQL audit config */
  ycql_audit_config?: YCQLAuditConfig;
  /** YSQL audit config */
  ysql_audit_config?: YSQLAuditConfig;
}

/**
 * Log Level
 */
export type YCQLAuditConfigLogLevel = 'INFO' | 'WARNING' | 'ERROR';

// eslint-disable-next-line @typescript-eslint/no-redeclare
export const YCQLAuditConfigLogLevel = {
  INFO: 'INFO' as YCQLAuditConfigLogLevel,
  WARNING: 'WARNING' as YCQLAuditConfigLogLevel,
  ERROR: 'ERROR' as YCQLAuditConfigLogLevel
};

export type YCQLAuditConfigIncludedCategoriesItem =
  | 'QUERY'
  | 'DML'
  | 'DDL'
  | 'DCL'
  | 'AUTH'
  | 'PREPARE'
  | 'ERROR'
  | 'OTHER';

// eslint-disable-next-line @typescript-eslint/no-redeclare
export const YCQLAuditConfigIncludedCategoriesItem = {
  QUERY: 'QUERY' as YCQLAuditConfigIncludedCategoriesItem,
  DML: 'DML' as YCQLAuditConfigIncludedCategoriesItem,
  DDL: 'DDL' as YCQLAuditConfigIncludedCategoriesItem,
  DCL: 'DCL' as YCQLAuditConfigIncludedCategoriesItem,
  AUTH: 'AUTH' as YCQLAuditConfigIncludedCategoriesItem,
  PREPARE: 'PREPARE' as YCQLAuditConfigIncludedCategoriesItem,
  ERROR: 'ERROR' as YCQLAuditConfigIncludedCategoriesItem,
  OTHER: 'OTHER' as YCQLAuditConfigIncludedCategoriesItem
};

export type YCQLAuditConfigExcludedCategoriesItem =
  | 'QUERY'
  | 'DML'
  | 'DDL'
  | 'DCL'
  | 'AUTH'
  | 'PREPARE'
  | 'ERROR'
  | 'OTHER';

// eslint-disable-next-line @typescript-eslint/no-redeclare
export const YCQLAuditConfigExcludedCategoriesItem = {
  QUERY: 'QUERY' as YCQLAuditConfigExcludedCategoriesItem,
  DML: 'DML' as YCQLAuditConfigExcludedCategoriesItem,
  DDL: 'DDL' as YCQLAuditConfigExcludedCategoriesItem,
  DCL: 'DCL' as YCQLAuditConfigExcludedCategoriesItem,
  AUTH: 'AUTH' as YCQLAuditConfigExcludedCategoriesItem,
  PREPARE: 'PREPARE' as YCQLAuditConfigExcludedCategoriesItem,
  ERROR: 'ERROR' as YCQLAuditConfigExcludedCategoriesItem,
  OTHER: 'OTHER' as YCQLAuditConfigExcludedCategoriesItem
};

/**
 * YCQL Audit Logging Configuration. Part of AuditLogConfig.
 */
export interface YCQLAuditConfig {
  /** Enabled */
  enabled: boolean;
  /** Excluded Categories */
  excluded_categories: YCQLAuditConfigExcludedCategoriesItem[];
  /** Excluded Keyspaces */
  excluded_keyspaces: string[];
  /** Excluded Users */
  excluded_users: string[];
  /** Included categories */
  included_categories: YCQLAuditConfigIncludedCategoriesItem[];
  /** Included Keyspaces */
  included_keyspaces: string[];
  /** Included Users */
  included_users: string[];
  /** Log Level */
  log_level: YCQLAuditConfigLogLevel;
}

/**
 * Additional tags
 */
export type UniverseLogsExporterConfigAdditionalTags = { [key: string]: string };

/**
 * Universe Logs Exporter Config. Part of AuditLogConfig.
 */
export interface UniverseLogsExporterConfig {
  /** Additional tags */
  additional_tags: UniverseLogsExporterConfigAdditionalTags;
  /** Exporter uuid */
  exporter_uuid: string;
}

/**
 * Data Placement Specification for the cluster. Part of ClusterSpec.

Note that this is optional to configure. YugabyteDB Anywhere will automatically place the data based on the available resources. If this data placement is configured, then YugabyteDB Anywhere will use this as a "hint" and the data will be placed based on this configuration on a best-effort basis.

 */
export interface ClusterPlacementSpec {
  cloud_list: PlacementCloud[];
}

/**
 * YBA Placement Availability Zone within Placement Region. Part of PlacementRegion.
 */
export interface PlacementAZ {
  /** The AZ id */
  uuid?: string;
  /** The AZ name */
  name?: string;
  /** The minimum number of copies of data we should place into this AZ. */
  replication_factor?: number;
  /** The subnet in the AZ. */
  subnet?: string;
  /** The secondary subnet in the AZ. */
  secondary_subnet?: string;
  /** Number of nodes in each AZ. */
  num_nodes_in_az?: number;
  /** Affinitizes raft leaders to this AZ. */
  leader_affinity?: boolean;
  /** The Load Balancer id. */
  lb_name?: string;
}

/**
 * YBA Placement Region. Part of PlacementCloud.
 */
export interface PlacementRegion {
  /** The region provider id. */
  uuid?: string;
  /** The actual provider given region code. */
  code?: string;
  /** The region name. */
  name?: string;
  /** The list of AZs inside this region into which we want to place data. */
  az_list?: PlacementAZ[];
  /** The Load Balancer FQDN. */
  lb_fqdn?: string;
}

/**
 * YBA Placement Cloud. Part of ClusterPlacementSpec.
 */
export interface PlacementCloud {
  /** The cloud provider id. */
  uuid?: string;
  /** The cloud provider code. */
  code?: string;
  /** The list of region in this cloud we want to place data in. */
  region_list?: PlacementRegion[];
  /** UUID of default region. For universes with more AZs than RF, the default placement for user tables will be RF AZs in the default region. This is commonly encountered in geo-partitioning use cases. */
  default_region?: string;
}

/**
 * Helm overrides per availability zone of this cluster. Applicable only if this is a k8s cloud provider. Refer https://github.com/yugabyte/charts/blob/master/stable/yugabyte/values.yaml for the list of supported overrides.
 */
export type ClusterProviderSpecAzHelmOverrides = { [key: string]: string };

/**
 * Cloud Provider settings for the cluster. Part of ClusterSpec.
 */
export interface ClusterProviderSpec {
  /** Cloud provider UUID */
  provider: string;
  /** The list of regions in the cloud provider to place data replicas */
  region_list?: string[];
  /** The region to nominate as the preferred region in a geo-partitioned multi-region cluster */
  preferred_region?: string;
  /** One of the SSH access keys defined in Cloud Provider to be configured on nodes VMs. Required for AWS, Azure and GCP Cloud Providers. */
  access_key_code?: string;
  /** The AWS IAM instance profile ARN to use for the nodes in this cluster. Applicable only for nodes on AWS Cloud Provider. If specified, YugabyteDB Anywhere will use this instance profile instead of the access key. */
  aws_instance_profile?: string;
  /** Image bundle UUID to use for node VM image. Refers to one of the image bundles defined in the cloud provider. */
  image_bundle_uuid?: string;
  /** Helm overrides for this cluster. Applicable only for a k8s cloud provider. Refer https://github.com/yugabyte/charts/blob/master/stable/yugabyte/values.yaml for the list of supported overrides. */
  helm_overrides?: string;
  /** Helm overrides per availability zone of this cluster. Applicable only if this is a k8s cloud provider. Refer https://github.com/yugabyte/charts/blob/master/stable/yugabyte/values.yaml for the list of supported overrides. */
  az_helm_overrides?: ClusterProviderSpecAzHelmOverrides;
}

/**
 * Universe Cluster Spec. Part of UniverseSpec.
 */
export interface ClusterSpec {
  /** System generated cluster uuid used to lookup corresponding ClusterInfo. This is not a user input. */
  readonly uuid?: string;
  /** Cluster type can be one of PRIMARY, ASYNC (for ReadOnly), ADDON */
  cluster_type: ClusterSpecClusterType;
  /** The number of nodes (tservers) to provision in this cluster */
  num_nodes: number;
  /** The number of copies of data to maintain in this cluster. Defaults to 3. */
  replication_factor?: number;
  /** Whether to run tserver and master processes in dedicated nodes in this cluster. Defaults to false where master and tserver processes share the same node. */
  dedicated_nodes?: boolean;
  node_spec: ClusterNodeSpec;
  networking_spec?: ClusterNetworkingSpec;
  provider_spec: ClusterProviderSpec;
  placement_spec?: ClusterPlacementSpec;
  /** Whether to use spot instances for nodes in aws/gcp. Used in dev/test environments. */
  use_spot_instance?: boolean;
  /** A map of strings representing a set of Tags and Values to apply on nodes in the aws/gcp/azu cloud. See https://docs.yugabyte.com/preview/yugabyte-platform/manage-deployments/instance-tags/. */
  instance_tags?: ClusterSpecInstanceTags;
  audit_log_config?: AuditLogConfig;
  /** The set of user defined gflags for tserver and master processes running on nodes. Can be changed using API /customers/:cUUID/universes/:uniUUID/upgrade/gflags. */
  gflags?: ClusterGFlags;
}

/**
 * Granular network settings overridden per Availability Zone identified by AZ uuid.
 */
export type ClusterNetworkingSpecAllOfAzNetworking = { [key: string]: AvailabilityZoneNetworking };

/**
 * Whether to create a load balancer service for this cluster. Defaults to NONE.
 */
export type ClusterNetworkingSpecAllOfEnableExposingService = 'NONE' | 'EXPOSED' | 'UNEXPOSED';

// eslint-disable-next-line @typescript-eslint/no-redeclare
export const ClusterNetworkingSpecAllOfEnableExposingService = {
  NONE: 'NONE' as ClusterNetworkingSpecAllOfEnableExposingService,
  EXPOSED: 'EXPOSED' as ClusterNetworkingSpecAllOfEnableExposingService,
  UNEXPOSED: 'UNEXPOSED' as ClusterNetworkingSpecAllOfEnableExposingService
};

export type ClusterNetworkingSpecAllOf = {
  /** Whether to create a load balancer service for this cluster. Defaults to NONE. */
  enable_exposing_service?: ClusterNetworkingSpecAllOfEnableExposingService;
  /** Create target groups if enabled. Used by YBM. */
  enable_lb?: boolean;
  /** Granular network settings overridden per Availability Zone identified by AZ uuid. */
  az_networking?: ClusterNetworkingSpecAllOfAzNetworking;
};

/**
 * The Proxy Settings for the nodes in the Universe. NodeProxyConfig is part of NodeSpec.
 */
export interface NodeProxyConfig {
  /** The HTTP_PROXY to use */
  http_proxy?: string;
  /** The HTTPS_PROXY to use */
  https_proxy?: string;
  /** The NO_PROXY settings. Should follow cURL no_proxy format */
  no_proxy_list?: string[];
}

/**
 * (Place holder for) Network settings that can be overridden per tserver or master process for nodes in the cluster. The node instances can be onprem nodes, VMs in GCP/AWS/Azure, or pods in k8s. Part of AvailabilityZoneNetworking.
 */
// eslint-disable-next-line @typescript-eslint/no-empty-interface
export interface PerProcessNetworking {}

/**
 * Networking properties for each node in the cluster. The settings can be configured at top-level for uniform node settings for both tserver and master nodes. Granular settings for tserver and master will be honored if provided (and dedicated_nodes is true or this is k8s cluster). Part of ClusterNetworkingSpec.
 */
export interface AvailabilityZoneNetworking {
  /** Network properties overridden for tserver nodes of cluster. */
  tserver?: PerProcessNetworking;
  /** Network properties overridden for master nodes of cluster. */
  master?: PerProcessNetworking;
  proxy_config?: NodeProxyConfig;
}

/**
 * The network settings configured at top-level are uniform settings for both tserver and master nodes. Granular settings for tserver and master (honoured if dedicated_nodes is true or this is k8s cluster) are available for certain network settings. Granular settings can also be overridden per Availability Zone. Part of ClusterSpec.
 */
export type ClusterNetworkingSpec = AvailabilityZoneNetworking & ClusterNetworkingSpecAllOf;

/**
 * Granular node settings overridden per Availability Zone idetified by AZ uuid.
 */
export type ClusterNodeSpecAllOfAzNodeSpec = { [key: string]: AvailabilityZoneNodeSpec };

/**
 * K8SNodeResourceSpec

Custom k8s resource spec is used to specify custom cpu and memory requests/limits for tserver and master pods of a cluster. Specified as part of ClusterCustomInstanceSpec.

 */
export interface K8SNodeResourceSpec {
  /** Number of CPU cores for tserver/master pods */
  cpu_core_count?: number;
  /** Memory in GiB for tserver/master pods */
  memory_gib?: number;
}

export type ClusterNodeSpecAllOf = {
  /** Used only for a k8s Universe. Required for k8s Universe if instance_type is not specified. Sets custom cpu and memory requests/limits for master pods of a cluster. */
  k8s_master_resource_spec?: K8SNodeResourceSpec;
  /** Used only for a k8s Universe. Required for k8s Universe if instance_type is not specified. Sets custom cpu and memory requests/limits for tserver pods of a cluster. */
  k8s_tserver_resource_spec?: K8SNodeResourceSpec;
  /** Granular node settings overridden per Availability Zone idetified by AZ uuid. */
  az_node_spec?: ClusterNodeSpecAllOfAzNodeSpec;
};

/**
 * Storage type used for this instance, if this is a aws (IO1, GP2, GP3), gcp (Scratch, Persistent) or azu (StandardSSD_LRS, Premium_LRS, UltraSSD_LRS) cluster.
 */
export type ClusterStorageSpecStorageType =
  | 'IO1'
  | 'GP2'
  | 'GP3'
  | 'Scratch'
  | 'Persistent'
  | 'StandardSSD_LRS'
  | 'Premium_LRS'
  | 'UltraSSD_LRS'
  | 'Local';

// eslint-disable-next-line @typescript-eslint/no-redeclare
export const ClusterStorageSpecStorageType = {
  IO1: 'IO1' as ClusterStorageSpecStorageType,
  GP2: 'GP2' as ClusterStorageSpecStorageType,
  GP3: 'GP3' as ClusterStorageSpecStorageType,
  Scratch: 'Scratch' as ClusterStorageSpecStorageType,
  Persistent: 'Persistent' as ClusterStorageSpecStorageType,
  StandardSSD_LRS: 'StandardSSD_LRS' as ClusterStorageSpecStorageType,
  Premium_LRS: 'Premium_LRS' as ClusterStorageSpecStorageType,
  UltraSSD_LRS: 'UltraSSD_LRS' as ClusterStorageSpecStorageType,
  Local: 'Local' as ClusterStorageSpecStorageType
};

/**
 * Storage volume specification that is used for tserver nodes in this cluster. Part of ClusterSpec, ClusterAddSpec and ClusterEditSpec.
 */
export interface ClusterStorageSpec {
  /** The size of each volume in each instance. Could be modified in payload for /resize_node API call */
  volume_size: number;
  /** Number of volumes to be mounted on this instance at the default path */
  num_volumes: number;
  /** Comma-separated list of mount points for the volumes in each instance. Required for an onprem cluster. */
  mount_points?: string;
  /** Name of the storage class, if this is a kubernetes cluster */
  storage_class?: string;
  /** Storage type used for this instance, if this is a aws (IO1, GP2, GP3), gcp (Scratch, Persistent) or azu (StandardSSD_LRS, Premium_LRS, UltraSSD_LRS) cluster. */
  storage_type?: ClusterStorageSpecStorageType;
  /** Desired IOPS for the volumes mounted on this aws, gcp or azu instance */
  disk_iops?: number;
  /** Desired throughput for the volumes mounted on this aws, gcp or azu instance */
  throughput?: number;
}

/**
 * Instance settings for each node in the cluster. The instances can be onprem nodes, VMs in GCP/AWS/Azure, or pods in k8s. Part of AvailabilityZoneNodeSpec.
 */
export interface PerProcessNodeSpec {
  /** Instance type for tserver/master nodes of cluster that determines the cpu and memory resources. */
  instance_type?: string;
  storage_spec?: ClusterStorageSpec;
}

export type AvailabilityZoneNodeSpecAllOf = {
  /** Amount of memory in MB to limit the postgres process using the ysql cgroup. The value should be greater than 0. When set to 0 it results in no cgroup limits. For a read replica cluster, setting this value to null or -1 would inherit this value from the primary cluster. Applicable only for nodes running as Linux VMs on AWS/GCP/Azure Cloud Provider. Only used internally by YBM. */
  cgroup_size?: number;
  /** Instance spec for tserver nodes of cluster. */
  tserver?: PerProcessNodeSpec;
  /** Instance spec for master nodes of cluster */
  master?: PerProcessNodeSpec;
};

/**
 * Properties for each node in the cluster. The settings can be configured at top-level for uniform node settings for both tserver and master nodes. Granular settings for tserver and master will be honored if provided (and dedicated_nodes is true or this is k8s cluster). Part of ClusterNodeSpec.
 */
export type AvailabilityZoneNodeSpec = PerProcessNodeSpec & AvailabilityZoneNodeSpecAllOf;

/**
 * Node settings (like CPU / memory) for each node in the cluster. The node settings configured at top-level are uniform settings for both tserver and master nodes. Granular settings for tserver and master (honoured if dedicated_nodes is true or this is k8s cluster) are available for certain node properties. Granular settings can also be overridden per Availability Zone. This is part of ClusterSpec.
 */
export type ClusterNodeSpec = AvailabilityZoneNodeSpec & ClusterNodeSpecAllOf;

/**
 * Communication Ports used by nodes of this Universe. Part of UniverseNetworkingSpec.
 */
export interface CommunicationPortsSpec {
  /** Master table HTTP port */
  master_http_port?: number;
  /** Master table RCP port */
  master_rpc_port?: number;
  /** Node exporter port */
  node_exporter_port?: number;
  /** Otel Collector metrics port */
  otel_collector_metrics_port?: number;
  /** Redis HTTP port */
  redis_server_http_port?: number;
  /** Redis RPC port */
  redis_server_rpc_port?: number;
  /** Tablet server HTTP port */
  tserver_http_port?: number;
  /** Tablet server RPC port */
  tserver_rpc_port?: number;
  /** Yb controller HTTP port */
  yb_controller_http_port?: number;
  /** Yb controller RPC port */
  yb_controller_rpc_port?: number;
  /** YQL HTTP port */
  yql_server_http_port?: number;
  /** YQL RPC port */
  yql_server_rpc_port?: number;
  /** YSQL HTTP port */
  ysql_server_http_port?: number;
  /** YSQL RPC port */
  ysql_server_rpc_port?: number;
}

/**
 * Networking specification that is used for nodes in this Universe. Part of UniverseSpec.
 */
export interface UniverseNetworkingSpec {
  communication_ports?: CommunicationPortsSpec;
  /** Whether to assign a public IP for nodes in this Universe. */
  assign_public_ip?: boolean;
  /** Whether to assign a static public IP for nodes in this Universe. */
  assign_static_public_ip?: boolean;
  /** Whether to enable IPv6 on nodes in this cluster. Defaults to false. */
  enable_ipv6?: boolean;
}

/**
 * YCQL Spec for the Universe. Part of UniverseSpec.
 */
export interface YCQLSpec {
  /** Whether to enable YCQL API on this Universe */
  enable?: boolean;
  /** Whether to enable authentication to access YCQL on this Universe */
  enable_auth?: boolean;
  /** Password to set for the YCQL database in this universe. Required if enable_auth is true. */
  password?: string;
}

/**
 * YSQL Spec for the Universe. Part of UniverseSpec.
 */
export interface YSQLSpec {
  /** Whether to enable YSQL API on this Universe */
  enable?: boolean;
  /** Whether to enable authentication to access YSQL on this Universe */
  enable_auth?: boolean;
  /** Password to set for the YSQL database in this universe. Required if enable_auth is true. */
  password?: string;
}

/**
 * Specification of node-to-node and client-to-node TLS encryption. Part of UniverseSpec.
 */
export interface EncryptionInTransitSpec {
  /** Whether to enable encryption for communication among DB nodes */
  enable_node_to_node_encrypt?: boolean;
  /** Whether to enable encryption for client connection to DB nodes */
  enable_client_to_node_encrypt?: boolean;
  /** The UUID of the rootCA to be used to generate node certificates and facilitate TLS communication between database nodes. */
  root_ca?: string;
  /** The UUID of the clientRootCA to be used to generate client certificates and facilitate TLS communication between server and client. Can be set to same as root_CA. */
  client_root_ca?: string;
}

/**
 * Encryption At Rest specification for the Universe. Part of UniverseSpec.
 */
export interface EncryptionAtRestSpec {
  /** The KMS Configuration associated with the encryption keys being used on this Universe */
  kms_config_uuid?: string;
}
