export const DrConfigActions = {
  CREATE: 'createDrConfig',
  DELETE: 'deleteDrConfig',
  EDIT: 'editDrConfig',
  EDIT_TARGET: 'editDrConfigTarget',
  INITIATE_FAILOVER: 'initiateFailover'
} as const;
export type DrConfigActions = typeof DrConfigActions[keyof typeof DrConfigActions];

export const FailoverType = {
  PLANNED: 'plannedFailover',
  UNPLANNED: 'unplannedFailover'
} as const;
export type FailoverType = typeof FailoverType[keyof typeof FailoverType];

export const RpoUnit = {
  SECOND: 'second',
  MINUTE: 'minute',
  HOUR: 'hour'
} as const;
export type RpoUnit = typeof RpoUnit[keyof typeof RpoUnit];

/**
 * Map from RPO units to milliseconds.
 */
export const RPO_UNIT_TO_SECONDS = {
  [RpoUnit.SECOND]: 1,
  [RpoUnit.MINUTE]: 60,
  [RpoUnit.HOUR]: 3_600
} as const;
