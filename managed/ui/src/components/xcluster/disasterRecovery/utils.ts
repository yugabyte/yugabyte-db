import { UnavailableUniverseStates } from '../../../redesign/helpers/constants';
import { getUniverseStatus } from '../../universes/helpers/universeHelpers';
import { DrConfigAction, DurationUnit, DURATION_UNIT_TO_SECONDS } from './constants';
import { assertUnreachableCase } from '../../../utils/errorHandlingUtils';

import { DrConfig, DrConfigSafetimeResponse, DrConfigState } from './dtos';
import { Universe } from '../../../redesign/helpers/dtos';
import { XClusterConfig } from '../dtos';

/**
 * Return a list of all enabled actions on a DR config.
 */
export const getEnabledDrConfigActions = (
  drConfig: DrConfig,
  sourceUniverse: Universe | undefined,
  targetUniverse: Universe | undefined
): DrConfigAction[] => {
  if (
    UnavailableUniverseStates.includes(getUniverseStatus(sourceUniverse).state) ||
    UnavailableUniverseStates.includes(getUniverseStatus(targetUniverse).state)
  ) {
    // xCluster 'Delete' action will fail on the backend. But if the user selects the
    // 'force delete' option, then they will be able to remove the config even if a
    // participating universe is unavailable.
    return [DrConfigAction.DELETE];
  }
  switch (drConfig.state) {
    case DrConfigState.INITIALIZING:
    case DrConfigState.SWITCHOVER_IN_PROGRESS:
    case DrConfigState.FAILOVER_IN_PROGRESS:
    case DrConfigState.FAILED:
    case DrConfigState.UPDATING:
      return [DrConfigAction.DELETE];
    case DrConfigState.REPLICATING:
      return [
        DrConfigAction.DELETE,
        DrConfigAction.EDIT,
        DrConfigAction.EDIT_TARGET,
        DrConfigAction.SWITCHOVER,
        DrConfigAction.FAILOVER
      ];
    case DrConfigState.HALTED:
      return [DrConfigAction.DELETE, DrConfigAction.EDIT, DrConfigAction.EDIT_TARGET];
    default:
      return assertUnreachableCase(drConfig.state);
  }
};

export const getNamespaceIdSafetimeEpochUsMap = (
  drConfigSafetimeResponse: DrConfigSafetimeResponse
) =>
  drConfigSafetimeResponse.safetimes.reduce<Record<string, number>>((namespaceIdSafetimeEpochUsMap, namespace) => {
    namespaceIdSafetimeEpochUsMap[namespace.namespaceId] = namespace.safetimeEpochUs;
    return namespaceIdSafetimeEpochUsMap;
  }, {});

/**
 * The minimum PITR retention period value is 1 (hour or day).
 */
export const getPitrRetentionPeriodMinValue = (_pitrRetentionPeriodUnit: DurationUnit | undefined) =>
  1;

/**
 * Formats seconds into a human-readable string showing the most appropriate unit.
 * Shows exact value without rounding, using the largest unit that divides evenly.
 */
export const formatRetentionPeriod = (seconds: number): string => {
  if (seconds <= 0) {
    return '0 seconds';
  }

  const SECONDS_PER_DAY = 86400;
  const SECONDS_PER_HOUR = 3600;
  const SECONDS_PER_MINUTE = 60;

  if (seconds % SECONDS_PER_DAY === 0) {
    const days = seconds / SECONDS_PER_DAY;
    return `${days} day${days !== 1 ? 's' : ''}`;
  }
  if (seconds % SECONDS_PER_HOUR === 0) {
    const hours = seconds / SECONDS_PER_HOUR;
    return `${hours} hour${hours !== 1 ? 's' : ''}`;
  }
  if (seconds % SECONDS_PER_MINUTE === 0) {
    const minutes = seconds / SECONDS_PER_MINUTE;
    return `${minutes} minute${minutes !== 1 ? 's' : ''}`;
  }
  return `${seconds} second${seconds !== 1 ? 's' : ''}`;
};

export const convertSecondsToLargestDurationUnit = (
  seconds: number,
  options: { noThrow?: boolean } = {}
): { value: number; unit: DurationUnit } => {
  const { noThrow = false } = options;

  if (seconds < 0 || isNaN(seconds)) {
    if (noThrow) {
      return { value: 1, unit: DurationUnit.HOUR };
    }
    throw new Error('Input must be a non-negative number');
  }

  if (seconds === 0) {
    return { value: 1, unit: DurationUnit.HOUR };
  }

  // Create an ordered array of units from largest to smallest based on DURATION_UNIT_TO_SECONDS
  const orderedUnits = Object.entries(DURATION_UNIT_TO_SECONDS)
    .sort(([, valueA], [, valueB]) => valueB - valueA)
    .map(([unit, _]) => unit as DurationUnit);

  for (const unit of orderedUnits) {
    const unitInSeconds = DURATION_UNIT_TO_SECONDS[unit];
    if (seconds % unitInSeconds === 0) {
      return { value: seconds / unitInSeconds, unit };
    }
  }

  // Fall back to hours if the value doesn't evenly divide into any unit
  const hoursInSeconds = DURATION_UNIT_TO_SECONDS[DurationUnit.HOUR];
  return { value: Math.max(1, Math.round(seconds / hoursInSeconds)), unit: DurationUnit.HOUR };
};

/**
 * Extract an XClusterConfig object from the fields of the provided DrConfig object.
 */
export const getXClusterConfig = (drConfig: DrConfig): XClusterConfig => ({
  automaticDdlMode: drConfig.automaticDdlMode,
  createTime: drConfig.createTime,
  modifyTime: drConfig.modifyTime,
  name: drConfig.name,
  paused: drConfig.paused,
  pitrConfigs: drConfig.pitrConfigs,
  replicationGroupName: drConfig.replicationGroupName,
  sourceActive: drConfig.primaryUniverseActive,
  status: drConfig.status,
  tableDetails: drConfig.tableDetails,
  tableType: 'YSQL',
  tables: drConfig.tables,
  targetActive: drConfig.drReplicaUniverseActive,
  type: drConfig.type,
  usedForDr: true,
  uuid: drConfig.xclusterConfigUuid,
  sourceUniverseState: drConfig.primaryUniverseState,
  sourceUniverseUUID: drConfig.primaryUniverseUuid,
  targetUniverseState: drConfig.drReplicaUniverseState,
  targetUniverseUUID: drConfig.drReplicaUniverseUuid
});
