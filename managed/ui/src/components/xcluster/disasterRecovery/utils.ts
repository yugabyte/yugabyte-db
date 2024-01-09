import { UnavailableUniverseStates } from '../../../redesign/helpers/constants';
import { getUniverseStatus } from '../../universes/helpers/universeHelpers';
import { DrConfigActions } from './constants';

import { DrConfig } from './types';
import { Universe } from '../../../redesign/helpers/dtos';
import { XClusterConfigStatus } from '../constants';
import { assertUnreachableCase } from '../../../utils/errorHandlingUtils';

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
  switch (drConfig.xClusterConfig.status) {
    case XClusterConfigStatus.INITIALIZED:
    case XClusterConfigStatus.UPDATING:
    case XClusterConfigStatus.FAILED:
    case XClusterConfigStatus.DELETED_UNIVERSE:
    case XClusterConfigStatus.DELETION_FAILED:
      return [DrConfigActions.DELETE];
    case XClusterConfigStatus.RUNNING:
      return [
        DrConfigActions.DELETE,
        DrConfigActions.EDIT,
        DrConfigActions.EDIT_TARGET,
        DrConfigActions.INITIATE_FAILOVER
      ];
    default:
      return assertUnreachableCase(drConfig.xClusterConfig.status);
  }
};
