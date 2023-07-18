import { isNonEmptyArray } from '../../../utils/ObjectUtils';
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

/**
 * Returns a universe status object with:
 *  - state - A universe state from the universe state mapping {@link UniverseState}
 *  - error - The error string from the current universe
 */
export const getUniverseStatus = (universe) => {
  const {
    updateInProgress,
    updateSucceeded,
    universePaused,
    errorString
  } = universe.universeDetails;

  if (!updateInProgress && updateSucceeded && !universePaused) {
    return { state: UniverseState.GOOD, error: errorString };
  }
  if (!updateInProgress && updateSucceeded && universePaused) {
    return { state: UniverseState.PAUSED, error: errorString };
  }
  if (updateInProgress) {
    return { state: UniverseState.PENDING, error: errorString };
  }
  if (!updateInProgress && !updateSucceeded) {
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
