import { UniverseState } from '../../components/universes/helpers/universeHelpers';

export const QueryApi = {
  YSQL: 'ysql',
  YCQL: 'ycql'
} as const;
export type QueryApi = typeof QueryApi[keyof typeof QueryApi];

export const YBTableRelationType = {
  SYSTEM_TABLE_RELATION: 'SYSTEM_TABLE_RELATION',
  USER_TABLE_RELATION: 'USER_TABLE_RELATION',
  INDEX_TABLE_RELATION: 'INDEX_TABLE_RELATION',
  MATVIEW_TABLE_RELATION: 'MATVIEW_TABLE_RELATION',
  COLOCATED_PARENT_TABLE_RELATION: 'COLOCATED_PARENT_TABLE_RELATION'
} as const;
export type YBTableRelationType = typeof YBTableRelationType[keyof typeof YBTableRelationType];

export const YBAHost = {
  GCP: 'gcp',
  AWS: 'aws',
  AZU: 'azu',
  SELF_HOSTED: 'selfHosted'
} as const;

export const UnavailableUniverseStates = [UniverseState.PAUSED, UniverseState.PENDING] as const;

/**
 * Values are mapped to the sort order strings from
 * react-boostrap-table ('asc', 'desc').
 */
export const SortOrder = {
  ASCENDING: 'asc',
  DESCENDING: 'desc'
} as const;
export type SortOrder = typeof SortOrder[keyof typeof SortOrder];

export const RuntimeConfigKey = {
  PROVIDER_REDESIGN_UI_FEATURE_FLAG: 'yb.ui.feature_flags.provider_redesign',
  EDIT_IN_USE_PORIVDER_UI_FEATURE_FLAG: 'yb.ui.feature_flags.edit_in_use_provider',
  XCLUSTER_TRANSACTIONAL_ATOMICITY_FEATURE_FLAG: 'yb.xcluster.transactional.enabled',
  DISASTER_RECOVERY_FEATURE_FLAG: 'yb.xcluster.dr.enabled',
  PERFOMANCE_ADVISOR_UI_FEATURE_FLAG: 'yb.ui.feature_flags.perf_advisor',
  GRANULAR_METRICS_FEATURE_FLAG: 'yb.ui.feature_flags.granular_metrics',
  IS_UNIVERSE_AUTH_ENFORCED: 'yb.universe.auth.is_enforced',
  USE_K8_CUSTOM_RESOURCES_FEATURE_FLAG: 'yb.use_k8s_custom_resources',
  IS_TAGS_ENFORCED: 'yb.universe.user_tags.is_enforced',
  DEFAULT_DEV_TAGS: 'yb.universe.user_tags.dev_tags',
  SHOW_DR_XCLUSTER_CONFIG: 'yb.ui.xcluster.dr.show_xcluster_config',
  IS_GFLAG_MULTILINE_ENABLED: 'yb.ui.feature_flags.gflag_multiline_conf',
  ENABLE_NODE_AGENT: 'yb.node_agent.client.enabled',
  GFLAGS_ALLOW_DURING_PREFINALIZE: 'yb.gflags.allow_during_prefinalize',
  RELEASES_REDESIGN_UI_FEATURE_FLAG: 'yb.ui.feature_flags.releases_redesign',
  ENABLE_DEDICATED_NODES: 'yb.ui.enable_dedicated_nodes',
  GEO_PARTITIONING_UI_FEATURE_FLAG: 'yb.universe.geo_partitioning_enabled',
  ENABLE_TROUBLESHOOTING: 'yb.ui.feature_flags.enable_troubleshooting'
} as const;

/**
 * Toast notification duration in milliseconds.
 */
export const ToastNotificationDuration = {
  SHORT: 2000,
  DEFAULT: 3000,
  LONG: 5000
} as const;

export const CHART_RESIZE_DEBOUNCE = 100;

// Universe actions map to frozen state
export const UNIVERSE_ACTION_TO_FROZEN_TASK_MAP = {
  // Overview tab
  UPGRADE_SOFTWARE: 'SoftwareUpgrade_Universe',
  UPGRADE_DB_VERSION: 'SoftwareUpgrade_Universe',
  ROLLBACK_UPGRADE: 'RollbackUpgrade_Universe',
  UPGRADE_LINUX_VERSION: 'VMImageUpgrade_Universe',
  UPGRADE_VM_IMAGE: 'VMImageUpgrade_Universe',
  UPGRADE_TO_SYSTEMD: 'SystemdUpgrade_Universe',
  UPGRADE_THIRD_PARTY_SOFTWARE: 'ThirdpartySoftwareUpgrade_Universe',
  EDIT_UNIVERSE: 'Update_Universe',
  EDIT_FLAGS: 'GFlagsUpgrade_Universe',
  EDIT_YSQL_CONFIG: 'ConfigureDBApis_Universe',
  EDIT_YCQL_CONFIG: 'ConfigureDBApis_Universe',
  EDIT_KUBERNETES_OVERRIDES: 'KubernetesOverridesUpgrade_Universe',
  INITIATE_ROLLING_RESTART: 'RestartUniverse_Universe',
  ADD_RR: 'Create_Cluster',
  EDIT_RR: 'Update_Universe',
  SUPPORT_BUNDLES: 'CreateSupportBundle_Universe',
  PAUSE_UNIVERSE: 'Pause_Universe',
  DELETE_UNIVERSE: 'Delete_Universe',
  ENCRYPTION_AT_REST: 'EnableEncryptionAtRest_Universe',
  ENCRYPTION_IN_TRANSIT: 'TlsToggle_Universe',

  // xCluster replication Tab - refer to the button where you can disbale (check api is called from)
  CONFIGURE_REPLICATION: 'Create_XClusterConfig',
  RESTART_REPLICATION: 'Restart_XClusterConfig',
  EDIT_REPLICATION: 'Edit_XClusterConfig',
  DELETE_REPLICATION: 'Delete_XClusterConfig',
  SYNC_REPLICATION: 'Sync_XClusterConfig',

  // xCluster DR tab -
  // refer to the button where you can disbale (Refer to api.ts for APIs like restartDrConfig, initiateFailover etc)
  CONFIGURE_DR: 'Create_DrConfig',
  DELETE_DR: 'Delete_DrConfig',
  SWITCHIVER_DR: 'Switchover_DrConfig',
  FAILOVER_DR: 'Failover_DrConfig',
  EDIT_DR: 'Edit_DrConfig',
  SYNC_DR: 'Sync_DrConfig',
  RESTART_DR: 'Restart_DrConfig',

  // Backups tab - Scheduled Backup policies -> BackupScheduleAPI.ts
  CREATE_SCHEDULED_POLICY: 'Create_Schedule',

  // Backups tab - PITR -> PitrAPI.ts
  ENABLE_PITR: 'CreatePitrConfig_Universe',
  DELETE_PITR: 'DeletePitrConfig_Universe',

  // Backups tab - Restore -> RestoreAPI.ts
  RESTORE_BACKUP: 'Restore_Backup',

  // Backups tab - Backups -> BackupAPI.ts
  CREATE_BACKUP: 'Create_Backup'
};

export const UNIVERSE_TASKS = {
  UPGRADE_SOFTWARE: 'UPGRADE_SOFTWARE',
  UPGRADE_DB_VERSION: 'UPGRADE_DB_VERSION',
  ROLLBACK_UPGRADE: 'ROLLBACK_UPGRADE',
  UPGRADE_LINUX_VERSION: 'UPGRADE_LINUX_VERSION',
  UPGRADE_VM_IMAGE: 'UPGRADE_VM_IMAGE',
  UPGRADE_TO_SYSTEMD: 'UPGRADE_TO_SYSTEMD',
  UPGRADE_THIRD_PARTY_SOFTWARE: 'UPGRADE_THIRD_PARTY_SOFTWARE',
  EDIT_UNIVERSE: 'EDIT_UNIVERSE',
  EDIT_FLAGS: 'EDIT_FLAGS',
  EDIT_YSQL_CONFIG: 'EDIT_YSQL_CONFIG',
  EDIT_YCQL_CONFIG: 'EDIT_YCQL_CONFIG',
  EDIT_KUBERNETES_OVERRIDES: 'EDIT_KUBERNETES_OVERRIDES',
  INITIATE_ROLLING_RESTART: 'INITIATE_ROLLING_RESTART',
  ADD_RR: 'ADD_RR',
  EDIT_RR: 'EDIT_RR',
  SUPPORT_BUNDLES: 'SUPPORT_BUNDLES',
  PAUSE_UNIVERSE: 'PAUSE_UNIVERSE',
  DELETE_UNIVERSE: 'DELETE_UNIVERSE',
  ENCRYPTION_AT_REST: 'ENCRYPTION_AT_REST',
  ENCRYPTION_IN_TRANSIT: 'ENCRYPTION_IN_TRANSIT',

  // xCluster replication actions
  CONFIGURE_REPLICATION: 'CONFIGURE_REPLICATION',
  RESTART_REPLICATION: 'RESTART_REPLICATION',
  EDIT_REPLICATION: 'EDIT_REPLICATION',
  DELETE_REPLICATION: 'DELETE_REPLICATION',
  SYNC_REPLICATION: 'SYNC_REPLICATION',

  // xCluster DR actions
  CONFIGURE_DR: 'CONFIGURE_DR',
  DELETE_DR: 'DELETE_DR',
  SWITCHIVER_DR: 'SWITCHIVER_DR',
  FAILOVER_DR: 'FAILOVER_DR',
  EDIT_DR: 'EDIT_DR',
  SYNC_DR: 'SYNC_DR',
  RESTART_DR: 'RESTART_DR',

  // Schedule Backups actions
  CREATE_SCHEDULED_POLICY: 'CREATE_SCHEDULED_POLICY',

  // PITR actions
  ENABLE_PITR: 'ENABLE_PITR',
  DELETE_PITR: 'DELETE_PITR',

  // Restore Backup actions
  RESTORE_BACKUP: 'RESTORE_BACKUP',

  // Backup actions
  CREATE_BACKUP: 'CREATE_BACKUP'
};
