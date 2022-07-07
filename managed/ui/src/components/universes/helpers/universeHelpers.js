import React from 'react';
import { isNonEmptyArray } from '../../../utils/ObjectUtils';
import { YBLoadingCircleIcon } from '../../common/indicators';

import _ from 'lodash';

/**
 * A mapping from universe state to display text and className.
 */
export const universeState = {
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
 *  - state - A universe state from the universe state mapping {@link universeState}
 *  - error - The error string from the current universe
 */
export const getUniverseStatus = (universe) => {
  const {
    updateInProgress,
    backupInProgress,
    updateSucceeded,
    universePaused,
    errorString
  } = universe.universeDetails;

  const taskInProgress = updateInProgress || backupInProgress;
  if (!taskInProgress && updateSucceeded && !universePaused) {
    return { state: universeState.GOOD, error: errorString };
  }
  if (!taskInProgress && updateSucceeded && universePaused) {
    return { state: universeState.PAUSED, error: errorString };
  }
  if (taskInProgress) {
    return { state: universeState.PENDING, error: errorString };
  }
  if (!taskInProgress && !updateSucceeded) {
    return errorString === 'Preflight checks failed.'
      ? { state: universeState.WARNING, error: errorString }
      : { state: universeState.BAD, error: errorString };
  }
  return { state: universeState.UNKNOWN, error: errorString };
};

export const getUniverseStatusIcon = (curStatus) => {
  if (_.isEqual(curStatus, universeState.GOOD)) {
    return <i className="fa fa-check-circle" />;
  }
  if (_.isEqual(curStatus, universeState.PAUSED)) {
    return <i className="fa fa-pause-circle-o" />;
  }
  if (_.isEqual(curStatus, universeState.PENDING)) {
    return <i className="fa fa-hourglass-half" />;
  }
  if (_.isEqual(curStatus, universeState.WARNING)) {
    return <i className="fa fa-warning" />;
  }
  if (_.isEqual(curStatus, universeState.BAD)) {
    return <i className="fa fa-warning" />;
  }
  if (_.isEqual(curStatus, universeState.UNKNOWN)) {
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
