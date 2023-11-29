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
  FAILED: 'Failed'
} as const;
export type XClusterConfigStatus = typeof XClusterConfigStatus[keyof typeof XClusterConfigStatus];

export const BROKEN_XCLUSTER_CONFIG_STATUSES: readonly XClusterConfigStatus[] = [
  XClusterConfigStatus.DELETED_UNIVERSE,
  XClusterConfigStatus.DELETION_FAILED
];

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
  UNABLE_TO_FETCH: 'UnableToFetch'
} as const;
export type XClusterTableStatus = typeof XClusterTableStatus[keyof typeof XClusterTableStatus];
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
  // Inenligible - No table with a matching identifier (keyspace, table and schema name)
  //               exists in the target universe
  INELIGIBLE_NO_MATCH: 'ineligibleNoMatch',

  // Eligible statuses:
  // Eligible - The table is not already in the current xCluster config
  ELIGIBLE_UNUSED: 'eligibleUnused',
  // Eligible - The table is already in the current xCluster config
  ELIGIBLE_IN_CURRENT_CONFIG: 'eligibleInCurrentConfig'
} as const;
export type XClusterTableEligibility = typeof XClusterTableEligibility[keyof typeof XClusterTableEligibility];

export const XCLUSTER_TABLE_INELIGIBLE_STATUSES: readonly XClusterTableEligibility[] = [
  XClusterTableEligibility.INELIGIBLE_IN_USE,
  XClusterTableEligibility.INELIGIBLE_NO_MATCH
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
//------------------------------------------------------------------------------------

// Time range selector constants

export const TIME_RANGE_TYPE = {
  HOURS: 'hours',
  DAYS: 'days',
  CUSTOM: 'custom'
} as const;

export const DROPDOWN_DIVIDER = {
  type: 'divider'
} as const;

export const DEFAULT_METRIC_TIME_RANGE_OPTION = {
  label: 'Last 1 hr',
  type: TIME_RANGE_TYPE.HOURS,
  value: '1'
} as const;

export const CUSTOM_METRIC_TIME_RANGE_OPTION = {
  label: 'Custom',
  type: TIME_RANGE_TYPE.CUSTOM
} as const;

/**
 * React-Bootstrap dropdown options used for constructing a time range selector.
 */
export const METRIC_TIME_RANGE_OPTIONS = [
  DEFAULT_METRIC_TIME_RANGE_OPTION,
  { label: 'Last 6 hrs', type: TIME_RANGE_TYPE.HOURS, value: '6' } as const,
  { label: 'Last 12 hrs', type: TIME_RANGE_TYPE.HOURS, value: '12' } as const,
  { label: 'Last 24 hrs', type: TIME_RANGE_TYPE.HOURS, value: '24' } as const,
  { label: 'Last 7 days', type: TIME_RANGE_TYPE.DAYS, value: '7' } as const,
  DROPDOWN_DIVIDER,
  CUSTOM_METRIC_TIME_RANGE_OPTION
] as const;

/**
 * Empty metric data to render an empty plotly graph when we are unable to provide real data.
 */
export const TABLE_LAG_GRAPH_EMPTY_METRIC: Metrics<'tserver_async_replication_lag_micros'> = {
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
  TSERVER_ASYNC_REPLICATION_LAG_METRIC: 'tserver_async_replication_lag_micros',
  DISK_USAGE: 'disk_usage'
} as const;
export type MetricName = typeof MetricName[keyof typeof MetricName];

// TODO: Add as type for layout alias keys in Metric type.
export const MetricTraceName = {
  [MetricName.TSERVER_ASYNC_REPLICATION_LAG_METRIC]: {
    COMMITTED_LAG: 'async_replication_committed_lag_micros',
    SENT_LAG: 'async_replication_sent_lag_micros'
  }
} as const;

export const REPLICATION_LAG_ALERT_NAME = 'Replication Lag';

export const PollingIntervalMs = {
  DR_CONFIG: 30_000,
  DR_CONFIG_STATE_TRANSITIONS: 10_000,
  XCLUSTER_CONFIG: 30_000,
  XCLUSTER_CONFIG_STATE_TRANSITIONS: 10_000,
  XCLUSTER_METRICS: 15_000
} as const;

export const XCLUSTER_METRIC_REFETCH_INTERVAL_MS = PollingIntervalMs.XCLUSTER_METRICS;
export const XCLUSTER_CONFIG_REFETCH_INTERVAL_MS = PollingIntervalMs.XCLUSTER_CONFIG;

export const XClusterModalName = {
  EDIT_CONFIG: 'editXClusterConfigModal',
  DELETE_CONFIG: 'deleteXClusterConfigModal',
  RESTART_CONFIG: 'restartXClusterConfigModal',
  ADD_TABLE_TO_CONFIG: 'addTablesToXClusterConfigModal',
  REMOVE_TABLE_FROM_CONFIG: 'removeTableFromXClusterConfigModal',
  TABLE_REPLICATION_LAG_GRAPH: 'tableReplicationLagGraphModal',
  SYNC_XCLUSTER_CONFIG_WITH_DB: 'syncXClusterConfigWithDB'
} as const;

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
