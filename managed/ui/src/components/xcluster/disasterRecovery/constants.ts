import { DrConfigState } from './dtos';

export const DrConfigActions = {
  CREATE: 'createDrConfig',
  DELETE: 'deleteDrConfig',
  EDIT: 'editDrConfig',
  EDIT_TARGET: 'editDrConfigTarget',
  SWITCHOVER: 'switchover',
  FAILOVER: 'failover'
} as const;
export type DrConfigActions = typeof DrConfigActions[keyof typeof DrConfigActions];

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
