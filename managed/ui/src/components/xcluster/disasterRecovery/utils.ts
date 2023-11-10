import { UnavailableUniverseStates } from '../../../redesign/helpers/constants';
import { getUniverseStatus } from '../../universes/helpers/universeHelpers';
import { DrConfigActions } from './constants';
import { assertUnreachableCase } from '../../../utils/errorHandlingUtils';

import { DrConfig, DrConfigSafetimeResponse, DrConfigState } from './dtos';
import { Universe } from '../../../redesign/helpers/dtos';

/**
 * Return a list of all enabled actions on a DR config.
 */
export const getEnabledDrConfigActions = (
  drConfig: DrConfig,
  sourceUniverse: Universe | undefined,
  targetUniverse: Universe | undefined
): DrConfigActions[] => {
  if (
    UnavailableUniverseStates.includes(getUniverseStatus(sourceUniverse).state) ||
    UnavailableUniverseStates.includes(getUniverseStatus(targetUniverse).state)
  ) {
    // xCluster 'Delete' action will fail on the backend. But if the user selects the
    // 'force delete' option, then they will be able to remove the config even if a
    // participating universe is unavailable.
    return [DrConfigActions.DELETE];
  }
  switch (drConfig.state) {
    case DrConfigState.INITIALIZING:
    case DrConfigState.SWITCHOVER_IN_PROGRESS:
    case DrConfigState.FAILOVER_IN_PROGRESS:
      return [DrConfigActions.DELETE];
    case DrConfigState.REPLICATING:
      return [
        DrConfigActions.DELETE,
        DrConfigActions.EDIT,
        DrConfigActions.EDIT_TARGET,
        DrConfigActions.SWITCHOVER,
        DrConfigActions.FAILOVER
      ];
    case DrConfigState.HALTED:
      return [DrConfigActions.DELETE, DrConfigActions.EDIT_TARGET];
    default:
      return assertUnreachableCase(drConfig.state);
  }
};

export const getNamespaceIdSafetimeEpochUsMap = (
  drConfigSafetimeResponse: DrConfigSafetimeResponse
) =>
  drConfigSafetimeResponse.safetimes.reduce((namespaceIdSafetimeEpochUsMap, namespace) => {
    namespaceIdSafetimeEpochUsMap[namespace.namespaceId] = namespace.safetimeEpochUs;
    return namespaceIdSafetimeEpochUsMap;
  }, {});
