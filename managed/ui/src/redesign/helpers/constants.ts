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
  IS_GFLAG_MULTILINE_ENABLED: 'yb.ui.feature_flags.gflag_multiline_conf',
  SHOW_DR_XCLUSTER_CONFIG: 'yb.ui.xcluster.dr.show_xcluster_config',
  ENABLE_DEDICATED_NODES: 'yb.ui.enable_dedicated_nodes',
  GEO_PARTITIONING_UI_FEATURE_FLAG: 'yb.universe.geo_partitioning_enabled',
  ENABLE_TROUBLESHOOTING: 'yb.ui.feature_flags.enable_troubleshooting'
} as const;

/**
 * Toast notification duration in milliseconds.
 */
export const ToastNotificationDuration = {
  SHORT: 2000,
  DEFAULT: 6000,
  LONG: 8000
} as const;

export const CHART_RESIZE_DEBOUNCE = 100;
