// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { ProgressBar } from 'react-bootstrap';
import { browserHistory } from 'react-router';
import { toast } from 'react-toastify';

import { isNonEmptyObject } from '../../../utils/ObjectUtils';
import { YBButton } from '../../common/forms/fields';
import { YBLoadingCircleIcon } from '../../common/indicators';
import {
  getUniversePendingTask,
  getUniverseStatus,
  hasPendingTasksForUniverse,
  universeState
} from '../helpers/universeHelpers';
import { UniverseAlertBadge } from '../YBUniverseItem/UniverseAlertBadge';

import './UniverseStatus.scss';

export default class UniverseStatus extends Component {
  componentDidUpdate(prevProps) {
    const {
      currentUniverse: { universeUUID, universeDetails },
      tasks: { customerTaskList },
      refreshUniverseData
    } = this.props;

    if (
      (universeDetails.updateInProgress || universeDetails.backupInProgress) &&
      !hasPendingTasksForUniverse(universeUUID, customerTaskList) &&
      hasPendingTasksForUniverse(universeUUID, prevProps.tasks.customerTaskList)
    ) {
      refreshUniverseData();
    }
  }

  redirectToTaskLogs = (taskUUID, universeUUID) => {
    taskUUID ? browserHistory.push(`/tasks/${taskUUID}`)
      : browserHistory.push(`/universes/${universeUUID}/tasks`);
  }

  handleRetryTaskClick = (taskUUID) => {
    this.props.retryCurrentTask(taskUUID).then((response) => {
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
    if (universeStatus.state === universeState.GOOD) {
      statusDisplay = (
        <div>
          <i className="fa fa-check-circle" />
          {showLabelText && universeStatus.state.text && <span>{universeStatus.state.text}</span>}
        </div>
      );
    } else if (universeStatus.state === universeState.PAUSED) {
      statusDisplay = (
        <div>
          <i className="fa fa-pause-circle-o" />
          {showLabelText && universeStatus.state.text && <span>{universeStatus.state.text}</span>}
        </div>
      );
    } else if (
      universeStatus.state === universeState.PENDING &&
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
      universeStatus.state === universeState.BAD ||
      universeStatus.state === universeState.WARNING
    ) {
      const currentUniverseFailedTask = customerTaskList?.filter((task) => {
        return ((task.targetUUID === currentUniverse.universeUUID) && task.status === "Failure")
      });
      const failedTask = currentUniverseFailedTask?.[0];
      statusDisplay = (
        <div className={showLabelText ? "status-error" : ""}>
          <i className="fa fa-warning" />
          {showLabelText && failedTask && <span className="status-error__reason">{`${failedTask.type} ${failedTask.target} failed`}</span>}
          {shouldDisplayTaskButton
            && !universePendingTask
            && (failedTask?.retryable
              ? <YBButton btnText={'Retry'} btnClass="btn btn-default retry-task-btn" onClick={() =>
                this.handleRetryTaskClick(failedTask.id)} />
              : <YBButton btnText={'View Details'} btnClass="btn btn-default view-task-details-btn" onClick={() =>
                this.redirectToTaskLogs(failedTask?.id, currentUniverse.universeUUID)} />
            )
          }
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
