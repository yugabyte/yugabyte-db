import { TableType } from '../../redesign/helpers/dtos';
import { Metrics } from './XClusterTypes';

//------------------------------------------------------------------------------------
// XCluster Status Constants
export const XClusterConfigStatus = {
  INITIALIZED: 'Initialized',
  RUNNING: 'Running',
  UPDATING: 'Updating',
  DELETED_UNIVERSE: 'DeletedUniverse',
  DELETION_FAILED: 'DeletionFailed',
  FAILED: 'Failed',
  DRAINED_DATA: 'DrainedData'
} as const;
export type XClusterConfigStatus = typeof XClusterConfigStatus[keyof typeof XClusterConfigStatus];

export const BROKEN_XCLUSTER_CONFIG_STATUSES: readonly XClusterConfigStatus[] = [
  XClusterConfigStatus.DELETED_UNIVERSE,
  XClusterConfigStatus.DELETION_FAILED
];

// In several places we assume that a corresponding task must be present when the
// xCluster config is in one of these statuses. If we decide later to introduce some
// transitory state for which the backend does not track task progress, then we must modify any
// reference that makes this assumption (i.e. transitory = running task exists)
export const TRANSITORY_XCLUSTER_CONFIG_STATUSES: readonly XClusterConfigStatus[] = [
  XClusterConfigStatus.INITIALIZED,
  XClusterConfigStatus.UPDATING
];

export const XClusterConfigState = {
  RUNNING: XClusterConfigStatus.RUNNING,
  PAUSED: 'Paused'
} as const;
export type XClusterConfigState = typeof XClusterConfigState[keyof typeof XClusterConfigState];

export const XClusterTableStatus = {
  RUNNING: 'Running',
  FAILED: 'Failed',
  WARNING: 'Warning',
  ERROR: 'Error',
  UPDATING: 'Updating',
  VALIDATED: 'Validated',
  BOOTSTRAPPING: 'Bootstrapping',
  UNABLE_TO_FETCH: 'UnableToFetch',
  // DROPPED - Client internal status. Does not exist on the backend.
  //            Used to mark tables which are dropped on both the source and target.
  DROPPED: 'Dropped',
  EXTRA_TABLE_ON_SOURCE: 'ExtraTableOnSource',
  EXTRA_TABLE_ON_TARGET: 'ExtraTableOnTarget',
  DROPPED_FROM_SOURCE: 'DroppedFromSource',
  DROPPED_FROM_TARGET: 'DroppedFromTarget',
  REPLICATION_ERROR: 'ReplicationError'
} as const;
export type XClusterTableStatus = typeof XClusterTableStatus[keyof typeof XClusterTableStatus];

/**
 * Tables status which indicate the table only exists on the target.
 */
export const SOURCE_MISSING_XCLUSTER_TABLE_STATUSES: readonly XClusterTableStatus[] = [
  XClusterTableStatus.DROPPED,
  XClusterTableStatus.DROPPED_FROM_SOURCE,
  XClusterTableStatus.EXTRA_TABLE_ON_TARGET
];

export const UNCONFIGURED_XCLUSTER_TABLE_STATUSES: readonly XClusterTableStatus[] = [
  XClusterTableStatus.EXTRA_TABLE_ON_SOURCE,
  XClusterTableStatus.EXTRA_TABLE_ON_TARGET
];

export const DROPPED_XCLUSTER_TABLE_STATUSES: readonly XClusterTableStatus[] = [
  XClusterTableStatus.DROPPED,
  XClusterTableStatus.DROPPED_FROM_SOURCE,
  XClusterTableStatus.DROPPED_FROM_TARGET
];

//------------------------------------------------------------------------------------

/**
 * Actions on an xCluster replication config.
 */
export const XClusterConfigAction = {
  CREATE: 'create',
  RESUME: 'resume',
  PAUSE: 'pause',
  RESTART: 'restart',
  DELETE: 'delete',
  ADD_TABLE: 'addTable',
  MANAGE_TABLE: 'manageTable',
  EDIT: 'edit',
  DB_SYNC: 'dbSync'
} as const;
export type XClusterConfigAction = typeof XClusterConfigAction[keyof typeof XClusterConfigAction];

export const XClusterConfigType = {
  BASIC: 'Basic',
  TXN: 'Txn'
} as const;
export type XClusterConfigType = typeof XClusterConfigType[keyof typeof XClusterConfigType];

export const XClusterConfigTypeLabel = {
  [XClusterConfigType.BASIC]: 'Basic',
  [XClusterConfigType.TXN]: 'Transactional'
} as const;

export const UniverseXClusterRole = {
  SOURCE: 'source',
  TARGET: 'target'
} as const;
export type UniverseXClusterRole = typeof UniverseXClusterRole[keyof typeof UniverseXClusterRole];

//------------------------------------------------------------------------------------
// Table Selection Constants

/**
 * This type stores whether a table is eligible to be in a particular xCluster config.
 */
export const XClusterTableEligibility = {
  // Ineligible statuses:
  // Ineligible - The table in use in another xCluster config
  INELIGIBLE_IN_USE: 'ineligibleInUse',

  // Eligible statuses:
  // Eligible - The table is not already in the current xCluster config
  ELIGIBLE_UNUSED: 'eligibleUnused',
  // Eligible - The table is already in the current xCluster config
  ELIGIBLE_IN_CURRENT_CONFIG: 'eligibleInCurrentConfig'
} as const;
export type XClusterTableEligibility = typeof XClusterTableEligibility[keyof typeof XClusterTableEligibility];

export const XCLUSTER_TABLE_INELIGIBLE_STATUSES: readonly XClusterTableEligibility[] = [
  XClusterTableEligibility.INELIGIBLE_IN_USE
] as const;

export const XCLUSTER_SUPPORTED_TABLE_TYPES = [
  TableType.PGSQL_TABLE_TYPE,
  TableType.YQL_TABLE_TYPE
] as const;
//------------------------------------------------------------------------------------

//------------------------------------------------------------------------------------
// Bootstrap Constants

// Validation
export const BOOTSTRAP_MIN_FREE_DISK_SPACE_GB = 100;

// This object stores strings which are used as keys to translation strings in en.json
// Any change to this object must be reflected in the en.json file as well.
export const BootstrapCategory = {
  NO_BOOTSTRAP_REQUIRED: 'noBootstrapRequired',
  TABLE_HAS_DATA_BIDIRECTIONAL: 'tableHasDataBidirectional',
  TARGET_TABLE_MISSING_BIDIRECTIONAL: 'targetTableMissingBidirectional',
  TABLE_HAS_DATA: 'tableHasData',
  TARGET_TABLE_MISSING: 'targetTableMissing'
} as const;
export type BootstrapCategory = typeof BootstrapCategory[keyof typeof BootstrapCategory];

//------------------------------------------------------------------------------------

// Time range selector constants

export const TimeRangeType = {
  HOURS: 'hours',
  DAYS: 'days',
  CUSTOM: 'custom'
} as const;

export const DROPDOWN_DIVIDER = {
  type: 'divider'
} as const;

export const DEFAULT_METRIC_TIME_RANGE_OPTION = {
  label: 'Last 1 hr',
  type: TimeRangeType.HOURS,
  value: '1'
} as const;

export const CUSTOM_METRIC_TIME_RANGE_OPTION = {
  label: 'Custom',
  type: TimeRangeType.CUSTOM
} as const;

/**
 * React-Bootstrap dropdown options used for constructing a time range selector.
 */
export const METRIC_TIME_RANGE_OPTIONS = [
  DEFAULT_METRIC_TIME_RANGE_OPTION,
  { label: 'Last 6 hrs', type: TimeRangeType.HOURS, value: '6' } as const,
  { label: 'Last 12 hrs', type: TimeRangeType.HOURS, value: '12' } as const,
  { label: 'Last 24 hrs', type: TimeRangeType.HOURS, value: '24' } as const,
  { label: 'Last 7 days', type: TimeRangeType.DAYS, value: '7' } as const,
  DROPDOWN_DIVIDER,
  CUSTOM_METRIC_TIME_RANGE_OPTION
] as const;

// We're only interested in the latest lag value to update the UI. Thus, we'll just request the
// last 1 hour of data.
export const liveMetricTimeRangeValue = '1';
export const liveMetricTimeRangeUnit = 'hours';

/**
 * Empty metric data to render an empty plotly graph when we are unable to provide real data.
 */
export const REPLICATION_LAG_GRAPH_EMPTY_METRIC: Metrics<'tserver_async_replication_lag_micros'> = {
  tserver_async_replication_lag_micros: {
    queryKey: 'tserver_async_replication_lag_micros',
    directURLs: [],
    layout: {
      title: 'Async Replication Lag',
      xaxis: {
        type: 'date',
        alias: {}
      },
      yaxis: {
        alias: {
          async_replication_sent_lag_micros: 'Sent Lag (Milliseconds)',
          async_replication_committed_lag_micros: 'Committed Lag (Milliseconds)'
        },
        ticksuffix: '&nbsp;ms'
      }
    },
    data: []
  }
};

/**
 * MetricName currently does not include all possible metric names.
 * Please update as needed.
 */
export const MetricName = {
  TSERVER_ASYNC_REPLICATION_LAG: 'tserver_async_replication_lag_micros',
  CONSUMER_SAFE_TIME_LAG: 'consumer_safe_time_lag',
  CONSUMER_SAFE_TIME_SKEW: 'consumer_safe_time_skew',
  ASYNC_REPLICATION_SENT_LAG: 'async_replication_sent_lag',
  DISK_USAGE: 'disk_usage',
  HA_BACKUP_LAG: 'yba_ha_backup_lag',
  HA_LAST_BACKUP_SIZE: 'yba_ha_last_backup_size_mb'
} as const;
export type MetricName = typeof MetricName[keyof typeof MetricName];

// TODO: Add as type for layout alias keys in Metric type.
export const MetricTraceName = {
  [MetricName.TSERVER_ASYNC_REPLICATION_LAG]: {
    COMMITTED_LAG: 'async_replication_committed_lag_micros',
    SENT_LAG: 'async_replication_sent_lag_micros'
  },
  [MetricName.CONSUMER_SAFE_TIME_LAG]: 'consumer_safe_time_lag'
} as const;
export type MetricTraceName =
  | 'async_replication_committed_lag_micros'
  | 'async_replication_sent_lag_micros'
  | 'consumer_safe_time_lag';

export const AlertName = {
  REPLICATION_LAG: 'Replication Lag',
  REPLICATION_SAFE_TIME_LAG: 'Replication Safe Time Lag'
} as const;

export const PollingIntervalMs = {
  UNIVERSE_STATE_TRANSITIONS: 10_000,
  DR_CONFIG: 30_000,
  DR_CONFIG_STATE_TRANSITIONS: 10_000,
  XCLUSTER_CONFIG: 30_000,
  XCLUSTER_CONFIG_STATE_TRANSITIONS: 10_000,
  XCLUSTER_METRICS: 15_000,
  ALERT_CONFIGURATION: 15_000
} as const;

export const XCLUSTER_METRIC_REFETCH_INTERVAL_MS = PollingIntervalMs.XCLUSTER_METRICS;
export const XCLUSTER_CONFIG_REFETCH_INTERVAL_MS = PollingIntervalMs.XCLUSTER_CONFIG;

export const XCLUSTER_UNDEFINED_LAG_NUMERIC_REPRESENTATION = -1;

/**
 * Constant value fallback. Used when runtime config value is invalid/undefined.
 */
export const XCLUSTER_TRANSACTIONAL_PITR_SNAPSHOT_INTERVAL_SECONDS_FALLBACK = 3600;
export const XCLUSTER_TRANSACTIONAL_PITR_RETENTION_PERIOD_SECONDS_FALLBACK = 3 * 24 * 60 * 60;

export const XClusterModalName = {
  EDIT_CONFIG: 'editXClusterConfigModal',
  DELETE_CONFIG: 'deleteXClusterConfigModal',
  RESTART_CONFIG: 'restartXClusterConfigModal',
  EDIT_TABLES: 'editTablesInXClusterConfigModal',
  ADD_TABLE_TO_CONFIG: 'addTablesToXClusterConfigModal',
  REMOVE_TABLE_FROM_CONFIG: 'removeTableFromXClusterConfigModal',
  TABLE_REPLICATION_LAG_GRAPH: 'tableReplicationLagGraphModal',
  SYNC_XCLUSTER_CONFIG_WITH_DB: 'syncXClusterConfigWithDB'
} as const;

export const XCLUSTER_UNIVERSE_TABLE_FILTERS = {
  xClusterSupportedOnly: true
};

/**
 * Standard input field width for all xCluster text fields and dropdowns.
 */
export const INPUT_FIELD_WIDTH_PX = 350;

/**
 * The name of the replication configuration cannot contain any characters in [SPACE '_' '*' '<' '>' '?' '|' '"' NULL])
 */
export const XCLUSTER_CONFIG_NAME_ILLEGAL_PATTERN = /[\s_*<>?|"\0]/;

/**
 * A YB software version must exceed the threshold to be considered a supported version.
 */
export const TRANSACTIONAL_ATOMICITY_YB_SOFTWARE_VERSION_THRESHOLD = '2.17.3.0-b1';

export const XCLUSTER_REPLICATION_DOCUMENTATION_URL =
  'https://docs.yugabyte.com/preview/yugabyte-platform/create-deployments/async-replication-platform/';
export const YB_ADMIN_XCLUSTER_DOCUMENTATION_URL =
  'https://docs.yugabyte.com/preview/admin/yb-admin/#xcluster-replication-commands';

export const I18N_KEY_PREFIX_XCLUSTER_TABLE_STATUS = 'clusterDetail.xCluster.config.tableStatus';
export const I18N_KEY_PREFIX_XCLUSTER_TERMS = 'clusterDetail.xCluster.terms';
