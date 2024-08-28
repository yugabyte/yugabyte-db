export const DrConfigAction = {
  CREATE: 'createDrConfig',
  DELETE: 'deleteDrConfig',
  EDIT: 'editDrConfig',
  EDIT_TARGET: 'editDrConfigTarget',
  SWITCHOVER: 'switchover',
  FAILOVER: 'failover'
} as const;
export type DrConfigAction = typeof DrConfigAction[keyof typeof DrConfigAction];

export const DurationUnit = {
  SECOND: 'second',
  MINUTE: 'minute',
  HOUR: 'hour',
  DAY: 'day'
} as const;
export type DurationUnit = typeof DurationUnit[keyof typeof DurationUnit];

/**
 * Map from duration units to seconds.
 *
 * The map should include all possible DurationUnit.
 */
export const DURATION_UNIT_TO_SECONDS: { [key in DurationUnit]: number } = {
  [DurationUnit.SECOND]: 1,
  [DurationUnit.MINUTE]: 60,
  [DurationUnit.HOUR]: 60 * 60,
  [DurationUnit.DAY]: 24 * 60 * 60
} as const;

/**
 * Standard width for all dropdown select components in the DR workflow.
 */
export const DR_DROPDOWN_SELECT_INPUT_WIDTH_PX = 350;

export const DOCS_URL_ACTIVE_ACTIVE_SINGLE_MASTER =
  'https://docs.yugabyte.com/preview/yugabyte-platform/back-up-restore-universes/disaster-recovery/';
export const DOCS_URL_DR_REPLICA_SELECTION_LIMITATIONS =
  'https://docs.yugabyte.com/preview/yugabyte-platform/back-up-restore-universes/disaster-recovery/disaster-recovery-setup/#prerequisites';
export const DOCS_URL_DR_SET_UP_REPLICATION_LAG_ALERT =
  'https://docs.yugabyte.com/stable/yugabyte-platform/back-up-restore-universes/disaster-recovery/disaster-recovery-setup/#set-up-replication-lag-alerts';
export const DOCS_URL_XCLUSTER_SET_UP_REPLICATION_LAG_ALERT =
  'https://docs.yugabyte.com/stable/yugabyte-platform/create-deployments/async-replication-platform/#set-up-replication-lag-alerts';
