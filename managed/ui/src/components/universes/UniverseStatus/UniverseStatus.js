// Copyright (c) YugaByte, Inc.

import { Component } from 'react';
import { ProgressBar } from 'react-bootstrap';
import { browserHistory } from 'react-router';

import { isNonEmptyObject } from '../../../utils/ObjectUtils';
import { YBButton } from '../../common/forms/fields';
import { YBLoadingCircleIcon } from '../../common/indicators';
import {
  getUniversePendingTask,
  getUniverseStatus,
  hasPendingTasksForUniverse,
  UniverseState
} from '../helpers/universeHelpers';
import { UniverseAlertBadge } from '../YBUniverseItem/UniverseAlertBadge';
import { RbacValidator } from '../../../redesign/features/rbac/common/RbacApiPermValidator';
import { ApiPermissionMap } from '../../../redesign/features/rbac/ApiAndUserPermMapping';

import './UniverseStatus.scss';

export default class UniverseStatus extends Component {
  componentDidUpdate(prevProps) {
    const {
      currentUniverse: { universeUUID, universeDetails },
      tasks: { customerTaskList },
      refreshUniverseData
    } = this.props;

    if (
      universeDetails.updateInProgress &&
      !hasPendingTasksForUniverse(universeUUID, customerTaskList) &&
      hasPendingTasksForUniverse(universeUUID, prevProps.tasks.customerTaskList)
    ) {
      refreshUniverseData();
    }
  }

  retryTaskClicked = (currentTaskUUID) => {
    this.props.retryCurrentTask(currentTaskUUID).then((response) => {
      const status = response?.payload?.response?.status || response?.payload?.status;
      if (status === 200 || status === 201) {
        browserHistory.push('/tasks');
      } else {
        const taskResponse = response?.payload?.response;
        const toastMessage = taskResponse?.data?.error
          ? taskResponse?.data?.error
          : taskResponse?.statusText;
        toast.error(toastMessage);
      }
    });
  };

  redirectToTaskLogs = (taskUUID, universeUUID) => {
    taskUUID
      ? browserHistory.push(`/tasks/${taskUUID}`)
      : browserHistory.push(`/universes/${universeUUID}/tasks`);
  };

  render() {
    const {
      currentUniverse,
      showLabelText,
      tasks: { customerTaskList },
      showAlertsBadge,
      shouldDisplayTaskButton
    } = this.props;

    const universeStatus = getUniverseStatus(currentUniverse);
    const universePendingTask = getUniversePendingTask(
      currentUniverse.universeUUID,
      customerTaskList
    );

    let statusDisplay = (
      <div className="status-pending-display-container">
        <YBLoadingCircleIcon size="small" />
        <span className="status-pending-name">{showLabelText && universeStatus.state.text}</span>
      </div>
    );
    if (universeStatus.state === UniverseState.GOOD) {
      statusDisplay = (
        <div>
          <i className="fa fa-check-circle" />
          {showLabelText && universeStatus.state.text && <span>{universeStatus.state.text}</span>}
        </div>
      );
    } else if (universeStatus.state === UniverseState.PAUSED) {
      statusDisplay = (
        <div>
          <i className="fa fa-pause-circle-o" />
          {showLabelText && universeStatus.state.text && <span>{universeStatus.state.text}</span>}
        </div>
      );
    } else if (
      universeStatus.state === UniverseState.PENDING &&
      isNonEmptyObject(universePendingTask)
    ) {
      if (showLabelText) {
        statusDisplay = (
          <div className="status-pending">
            <div className="status-pending-display-container">
              <YBLoadingCircleIcon size="small" />
              <span className="status-pending-name">
                {universeStatus.state.text}&hellip;
                {universePendingTask.percentComplete}%
              </span>
              <span className="status-pending-progress-container">
                <ProgressBar
                  className={'pending-action-progress'}
                  now={universePendingTask.percentComplete}
                />
              </span>
            </div>
          </div>
        );
      } else {
        statusDisplay = (
          <div className={'yb-orange'}>
            <YBLoadingCircleIcon size="small" />
          </div>
        );
      }
    } else if (
      universeStatus.state === UniverseState.BAD ||
      universeStatus.state === UniverseState.WARNING
    ) {
      const currentUniverseFailedTask = customerTaskList?.filter((task) => {
        return (
          task.targetUUID === currentUniverse.universeUUID &&
          (task.status === 'Failure' || task.status === 'Aborted')
        );
      });
      const failedTask = currentUniverseFailedTask?.[0];
      statusDisplay = (
        <div className={showLabelText ? 'status-error' : ''}>
          <i className="fa fa-warning" />
          {showLabelText &&
            (failedTask ? (
              <span className="status-error__reason">{`${failedTask.type} ${failedTask.target} failed`}</span>
            ) : (
              <span>{universeStatus.state.text}</span>
            ))}
          {shouldDisplayTaskButton && !universePendingTask && (
            <YBButton
              btnText={'View Details'}
              btnClass="btn btn-default view-task-details-btn"
              onClick={() => this.redirectToTaskLogs(failedTask?.id, currentUniverse.universeUUID)}
            />
          )}
          {shouldDisplayTaskButton && !universePendingTask && failedTask?.retryable && (
            <RbacValidator
              accessRequiredOn={{
                onResource: currentUniverse.universeUUID,
                ...ApiPermissionMap.RETRY_TASKS
              }}
              isControl
            >
              <YBButton
                btnText={'Retry Task'}
                btnClass="btn btn-default view-task-details-btn"
                onClick={() => this.retryTaskClicked(failedTask.id, currentUniverse.universeUUID)}
              />
            </RbacValidator>
          )}
        </div>
      );
    }

    return (
      <div className={'universe-status ' + universeStatus.state.className}>
        {statusDisplay}
        {showAlertsBadge && <UniverseAlertBadge universeUUID={currentUniverse.universeUUID} />}
      </div>
    );
  }
}
