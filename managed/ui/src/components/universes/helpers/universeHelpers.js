import { isNonEmptyArray, isNonEmptyObject, isDefinedNotNull } from '../../../utils/ObjectUtils';

export const getUniverseStatus = (universe, universePendingTask) => {
  const {
    updateInProgress,
    updateSucceeded,
    universePaused,
    errorString
  } = universe.universeDetails;

  // statusText stores the status for display
  // warning stores extra information for internal use (ex. warning icons for certain errors)
  let statusText = '';
  let warning = '';
  if (!isDefinedNotNull(universePendingTask) && updateSucceeded && !universePaused) {
    statusText = 'Ready';
  } else if (!isDefinedNotNull(universePendingTask) && updateSucceeded && universePaused) {
    statusText = 'Paused';
  } else if (updateInProgress && isNonEmptyObject(universePendingTask)) {
    statusText = 'Pending';
  } else if (!updateInProgress && !updateSucceeded) {
    statusText = errorString === 'Preflight checks failed.' ? 'Ready' : 'Error';
    warning = errorString;
  }
  return { statusText, warning };
};

export const getUniversePendingTask = (universeUUID, customerTaskList) => {
  return isNonEmptyArray(customerTaskList)
    ? customerTaskList.find(function (taskItem) {
        return (
          taskItem.targetUUID === universeUUID &&
          (taskItem.status === 'Running' || taskItem.status === 'Initializing') &&
          Number(taskItem.percentComplete) !== 100 &&
          taskItem.target.toLowerCase() !== 'backup'
        );
      })
    : null;
};
