import { isDefinedNotNull, isNonEmptyArray } from '../../../utils/ObjectUtils';
import { YBLoadingCircleIcon } from '../../common/indicators';

import _ from 'lodash';

/**
 * A mapping from universe state to display text and className.
 */
export const UniverseState = {
  GOOD: {
    text: 'Ready',
    className: 'good'
  },
  PAUSED: {
    text: 'Paused',
    className: 'paused'
  },
  PENDING: {
    text: 'Pending',
    className: 'pending'
  },
  WARNING: {
    text: 'Ready',
    className: 'warning'
  },
  BAD: {
    text: 'Error',
    className: 'bad'
  },
  UNKNOWN: {
    text: 'Loading',
    className: 'unknown'
  }
};

export const SoftwareUpgradeState = {
  READY: 'Ready',
  UPGRADING: 'Upgrading',
  UPGRADE_FAILED: 'UpgradeFailed',
  PRE_FINALIZE: 'PreFinalize',
  FINALIZING: 'Finalizing',
  FINALIZE_FAILED: 'FinalizeFailed',
  ROLLING_BACK: 'RollingBack',
  ROLLBACK_FAILED: 'RollbackFailed'
};

export const SoftwareUpgradeTaskType = {
  SOFTWARE_UPGRADE: 'SoftwareUpgrade',
  SOFTWARE_UPGRADEYB: 'SoftwareUpgradeYB',
  FINALIZE_UPGRADE: 'FinalizeUpgrade',
  ROLLBACK_UPGRADE: 'RollbackUpgrade'
};

/**
 * Returns a universe status object with:
 *  - state - A universe state from the universe state mapping {@link UniverseState}
 *  - error - The error string from the current universe
 */
export const getUniverseStatus = (universe) => {
  if (!universe?.universeDetails) {
    return { state: UniverseState.UNKNOWN, error: '' };
  }
  const {
    updateInProgress,
    updateSucceeded,
    universePaused,
    placementModificationTaskUuid,
    errorString
  } = universe.universeDetails;
  /* TODO: Using placementModificationTaskUuid is a short term fix to not clear universe error
   * state because updateSucceeded reports the state of the latest task only. This will be
   * replaced by backend driven APIs in future.
   */
  const allUpdatesSucceeded = updateSucceeded && !isDefinedNotNull(placementModificationTaskUuid);
  if (!updateInProgress && allUpdatesSucceeded && !universePaused) {
    return { state: UniverseState.GOOD, error: errorString };
  }
  if (!updateInProgress && allUpdatesSucceeded && universePaused) {
    return { state: UniverseState.PAUSED, error: errorString };
  }
  if (updateInProgress) {
    return { state: UniverseState.PENDING, error: errorString };
  }
  if (!updateInProgress && !allUpdatesSucceeded) {
    return errorString === 'Preflight checks failed.'
      ? { state: UniverseState.WARNING, error: errorString }
      : { state: UniverseState.BAD, error: errorString };
  }
  return { state: UniverseState.UNKNOWN, error: errorString };
};

export const getUniverseStatusIcon = (curStatus) => {
  if (_.isEqual(curStatus, UniverseState.GOOD)) {
    return <i className="fa fa-check-circle" />;
  }
  if (_.isEqual(curStatus, UniverseState.PAUSED)) {
    return <i className="fa fa-pause-circle-o" />;
  }
  if (_.isEqual(curStatus, UniverseState.PENDING)) {
    return <i className="fa fa-hourglass-half" />;
  }
  if (_.isEqual(curStatus, UniverseState.WARNING)) {
    return <i className="fa fa-warning" />;
  }
  if (_.isEqual(curStatus, UniverseState.BAD)) {
    return <i className="fa fa-warning" />;
  }
  if (_.isEqual(curStatus, UniverseState.UNKNOWN)) {
    return <YBLoadingCircleIcon size="small" />;
  }
};

export const isPendingUniverseTask = (universeUUID, taskItem) => {
  return (
    taskItem.targetUUID === universeUUID &&
    (taskItem.status === 'Running' || taskItem.status === 'Initializing') &&
    Number(taskItem.percentComplete) !== 100
  );
};

export const getUniversePendingTask = (universeUUID, customerTaskList) => {
  return isNonEmptyArray(customerTaskList)
    ? customerTaskList.find((taskItem) => isPendingUniverseTask(universeUUID, taskItem))
    : null;
};

export const hasPendingTasksForUniverse = (universeUUID, customerTaskList) => {
  return isNonEmptyArray(customerTaskList)
    ? customerTaskList.some((taskItem) => isPendingUniverseTask(universeUUID, taskItem))
    : false;
};

export const getcurrentUniverseFailedTask = (currentUniverse, customerTaskList) => {
  const latestTask = customerTaskList?.find((task) => {
    return task.targetUUID === currentUniverse.universeUUID;
  });
  if (latestTask && (latestTask.status === 'Failure' || latestTask.status === 'Aborted')) {
    return latestTask;
  }
  const universeDetails = currentUniverse.universeDetails;
  // Last universe task succeeded, but there can be a placement modification task failure.
  if (isDefinedNotNull(universeDetails.placementModificationTaskUuid)) {
    return customerTaskList?.find((task) => {
      return (
        task.targetUUID === currentUniverse.universeUUID &&
        task.id === universeDetails.placementModificationTaskUuid
      );
    });
  }
  return null;
};
