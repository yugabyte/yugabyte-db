import type { DropReason as DropReasonType } from 'react-beautiful-dnd';

export const DbUpgradeFormStep = {
  DB_VERSION: 'DB_VERSION',
  UPGRADE_METHOD: 'UPGRADE_METHOD',
  UPGRADE_PLAN: 'UPGRADE_PLAN',
  UPGRADE_PACE: 'UPGRADE_PACE'
} as const;
export type DbUpgradeFormStep = (typeof DbUpgradeFormStep)[keyof typeof DbUpgradeFormStep];
export const DB_UPGRADE_FIRST_FORM_STEP = DbUpgradeFormStep.DB_VERSION;

export const UpgradeMethod = {
  EXPRESS: 'EXPRESS',
  CANARY: 'CANARY'
} as const;
export type UpgradeMethod = (typeof UpgradeMethod)[keyof typeof UpgradeMethod];

/** Only rolling upgrade is supported when upgradeMethod is CANARY. */
export const UpgradePace = {
  ROLLING: 'ROLLING',
  CONCURRENT: 'CONCURRENT'
} as const;
export type UpgradePace = (typeof UpgradePace)[keyof typeof UpgradePace];

export const DEFAULT_WAIT_BETWEEN_BATCHES_SECONDS = 180;

/**
 * Runtime constants for DropResult.reason.
 * This is typed against the react-beautiful-dnd library.
 */
export const DropReason: Record<DropReasonType, DropReasonType> = {
  DROP: 'DROP',
  CANCEL: 'CANCEL'
};

export const AzClusterKind = {
  PRIMARY: 'primary',
  READ_REPLICA: 'read-replica'
} as const;
export type AzClusterKind = (typeof AzClusterKind)[keyof typeof AzClusterKind];

export const YBA_UNIVERSE_UPGRADE_DOCUMENTATION_URL =
  'https://docs.yugabyte.com/stable/yugabyte-platform/manage-deployments/upgrade-software/';
export const YBA_YSQL_MAJOR_UPGRADE_DOCUMENTATION_URL =
  'https://docs.yugabyte.com/stable/yugabyte-platform/manage-deployments/ysql-major-upgrade-yba/';
export const YBA_UNIVERSE_UPGRADE_EVALUATION_LINK =
  'https://docs.yugabyte.com/stable/yugabyte-platform/manage-deployments/upgrade-software-install/#monitor-the-universe';
