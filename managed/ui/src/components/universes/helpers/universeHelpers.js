import React from 'react';
import { isNonEmptyArray, isNonEmptyObject, isDefinedNotNull } from '../../../utils/ObjectUtils';
import { YBLoadingCircleIcon } from '../../common/indicators';

import _ from 'lodash';

export const status = {
  GOOD: {
    statusText: 'Ready',
    statusClassName: 'good'
  },
  PAUSED: {
    statusText: 'Paused',
    statusClassName: 'paused'
  },
  PENDING: {
    statusText: 'Pending',
    statusClassName: 'pending'
  },
  WARNING: {
    statusText: 'Ready',
    statusClassName: 'warning'
  },
  BAD: {
    statusText: 'Error',
    statusClassName: 'bad'
  },
  UNKNOWN: {
    statusText: 'Loading',
    statusClassName: 'unknown'
  }
};

export const getUniverseStatus = (universe, universePendingTask) => {
  const {
    updateInProgress,
    updateSucceeded,
    universePaused,
    errorString
  } = universe.universeDetails;

  // statusText stores the status for display
  // warning stores extra information for internal use (ex. warning icons for certain errors)
  if (!isDefinedNotNull(universePendingTask) && updateSucceeded && !universePaused) {
    return { status: status.GOOD, warning: '' };
  }
  if (!isDefinedNotNull(universePendingTask) && updateSucceeded && universePaused) {
    return { status: status.PAUSED, warning: '' };
  }
  if (updateInProgress && isNonEmptyObject(universePendingTask)) {
    return { status: status.PENDING, warning: '' };
  }
  if (!updateInProgress && !updateSucceeded) {
    return errorString === 'Preflight checks failed.'
      ? { status: status.WARNING, warning: errorString }
      : { status: status.BAD, warning: errorString };
  }
  return { status: status.UNKNOWN, warning: '' };
};

export const getUniverseStatusIcon = (curStatus) => {
  if (_.isEqual(curStatus, status.GOOD)) {
    return <i className="fa fa-check-circle" />;
  }
  if (_.isEqual(curStatus, status.PAUSED)) {
    return <i className="fa fa-pause-circle-o" />;
  }
  if (_.isEqual(curStatus, status.PENDING)) {
    return <i className="fa fa-hourglass-half" />;
  }
  if (_.isEqual(curStatus, status.WARNING)) {
    return <i className="fa fa-warning" />;
  }
  if (_.isEqual(curStatus, status.BAD)) {
    return <i className="fa fa-warning" />;
  }
  if (_.isEqual(curStatus, status.UNKNOWN)) {
    return <YBLoadingCircleIcon size="small" />;
  }
};

export const isPendingUniverseTask = (universeUUID, taskItem) => {
  return (
    taskItem.targetUUID === universeUUID &&
    (taskItem.status === 'Running' || taskItem.status === 'Initializing') &&
    Number(taskItem.percentComplete) !== 100 &&
    taskItem.target.toLowerCase() !== 'backup'
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
