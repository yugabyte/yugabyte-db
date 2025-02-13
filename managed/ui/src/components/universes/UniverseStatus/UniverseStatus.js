// Copyright (c) YugaByte, Inc.

import { Component } from 'react';
import _ from 'lodash';
import { ProgressBar } from 'react-bootstrap';
import { browserHistory } from 'react-router';

import { isNonEmptyObject } from '../../../utils/ObjectUtils';
import { YBButton } from '../../common/forms/fields';
import { YBLoadingCircleIcon } from '../../common/indicators';
import {
  getUniversePendingTask,
  getUniverseStatus,
  hasPendingTasksForUniverse,
  getcurrentUniverseFailedTask,
  UniverseState,
  SoftwareUpgradeState,
  SoftwareUpgradeTaskType
} from '../helpers/universeHelpers';
import { UniverseAlertBadge } from '../YBUniverseItem/UniverseAlertBadge';
import { TaskDetailSimpleComp } from '../../../redesign/features/tasks/components/TaskDetailSimpleComp';
import { RbacValidator } from '../../../redesign/features/rbac/common/RbacApiPermValidator';
import { ApiPermissionMap } from '../../../redesign/features/rbac/ApiAndUserPermMapping';
//icons
import WarningExclamation from '../images/warning_exclamation.svg';

import './UniverseStatus.scss';

export default class UniverseStatus extends Component {
  componentDidUpdate(prevProps) {
    const {
      currentUniverse: { universeUUID, universeDetails },
      tasks: { customerTaskList },
      refreshUniverseData,
    } = this.props;

    if (
      universeDetails.updateInProgress &&
      !hasPendingTasksForUniverse(universeUUID, customerTaskList) &&
      hasPendingTasksForUniverse(universeUUID, prevProps.tasks.customerTaskList)
    ) {
      refreshUniverseData();
    }
  }

  retryTaskClicked = (currentTaskUUID, universeUUID) => {
    this.props.retryCurrentTask(currentTaskUUID).then((response) => {
      const status = response?.payload?.response?.status || response?.payload?.status;
      if (status === 200 || status === 201) {
        browserHistory.push(`/universes/${universeUUID}/tasks`);
      } else {
        const taskResponse = response?.payload?.response;
        const toastMessage = taskResponse?.data?.error
          ? taskResponse?.data?.error
          : taskResponse?.statusText;
        toast.error(toastMessage);
      }
    });
  };

  rollbackTaskClicked = (currentTaskUUID, universeUUID) => {
    this.props.rollbackCurrentTask(currentTaskUUID).then((response) => {
      const status = response?.payload?.response?.status || response?.payload?.status;
      if (status === 200 || status === 201) {
        browserHistory.push(`/universes/${universeUUID}/tasks`);
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
      shouldDisplayTaskButton,
      runtimeConfigs,
      showTaskDetails = false
    } = this.props;

    const isRollBackFeatEnabled =
      runtimeConfigs?.data?.configEntries?.find(
        (c) => c.key === 'yb.upgrade.enable_rollback_support'
      )?.value === 'true';

    const universeStatus = getUniverseStatus(currentUniverse);
    const universePendingTask = getUniversePendingTask(
      currentUniverse.universeUUID,
      customerTaskList
    );
    let taskToDisplayInDrawer = universePendingTask;
    const universeUpgradeState = _.get(currentUniverse, 'universeDetails.softwareUpgradeState');
    let statusDisplay = (
      <div className="status-pending-display-container">
        <YBLoadingCircleIcon size="small" />
        <span className="status-pending-name">{showLabelText && universeStatus.state.text}</span>
      </div>
    );
    if (universeStatus.state === UniverseState.GOOD) {
      const failedTask = getcurrentUniverseFailedTask(currentUniverse, customerTaskList);
      statusDisplay = (
        <div>
          <i className="fa fa-check-circle" />
          {showLabelText && universeStatus.state.text && <span>{universeStatus.state.text}</span>}
        </div>
      );
      if (isRollBackFeatEnabled) {
        if (universeUpgradeState === SoftwareUpgradeState.PRE_FINALIZE) {
          statusDisplay = (
            <div className="pre-finalize-pending">
              <img src={WarningExclamation} height={'22px'} width={'22px'} alt="--" />
              {showLabelText && universeStatus.state.text && (
                <span>Pending upgrade finalization</span>
              )}
            </div>
          );
        }
        if (failedTask?.type === SoftwareUpgradeTaskType.SOFTWARE_UPGRADE) {
          taskToDisplayInDrawer = failedTask;
          statusDisplay = (
            <div className="pre-finalize-pending">
              <img src={WarningExclamation} height={'22px'} width={'22px'} alt="--" />
              {showLabelText && universeStatus.state.text && <span>Upgrade pre-check failed</span>}
              {shouldDisplayTaskButton && (
                <YBButton
                  btnText={'View Details'}
                  btnClass="btn btn-default view-task-details-btn"
                  onClick={() =>
                    this.redirectToTaskLogs(failedTask?.id, currentUniverse.universeUUID)
                  }
                />
              )}
              {failedTask?.retryable && shouldDisplayTaskButton && (
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
                    onClick={() =>
                      this.retryTaskClicked(failedTask?.id, currentUniverse.universeUUID)
                    }
                  />
                </RbacValidator>
              )}
            </div>
          );
        }
      }
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
      const failedTask = getcurrentUniverseFailedTask(currentUniverse, customerTaskList);
      taskToDisplayInDrawer = failedTask;
      statusDisplay = (
        <div className={showLabelText ? 'status-error' : ''}>
          <i className="fa fa-warning" />
          {showLabelText &&
            (failedTask ? (
              <span
                className={
                  ![
                    SoftwareUpgradeTaskType.ROLLBACK_UPGRADE,
                    SoftwareUpgradeTaskType.SOFTWARE_UPGRADE
                  ].includes(failedTask?.type) || !isRollBackFeatEnabled
                    ? 'status-error__reason'
                    : 'status-error__noHideText'
                }
              >{`${failedTask?.type} ${failedTask?.target} failed`}</span>
            ) : (
              <span>{universeStatus.state.text}</span>
            ))}
          {shouldDisplayTaskButton &&
            !universePendingTask &&
            failedTask !== undefined &&
            (![
              SoftwareUpgradeTaskType.ROLLBACK_UPGRADE,
              SoftwareUpgradeTaskType.SOFTWARE_UPGRADE
            ].includes(failedTask?.type) ||
              !isRollBackFeatEnabled) && (
              <YBButton
                btnText={'View Details'}
                btnClass="btn btn-default view-task-details-btn"
                onClick={() =>
                  this.redirectToTaskLogs(failedTask?.id, currentUniverse.universeUUID)
                }
              />
            )}
          {shouldDisplayTaskButton &&
            !universePendingTask &&
            failedTask !== undefined &&
            failedTask?.retryable &&
            (![
              SoftwareUpgradeTaskType.ROLLBACK_UPGRADE,
              SoftwareUpgradeTaskType.SOFTWARE_UPGRADE
            ].includes(failedTask?.type) ||
              !isRollBackFeatEnabled) && (
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
                  onClick={() =>
                    this.retryTaskClicked(failedTask?.id, currentUniverse.universeUUID)
                  }
                />
              </RbacValidator>
            )}
          {shouldDisplayTaskButton &&
            !universePendingTask &&
            failedTask !== undefined &&
            failedTask?.canRollback &&
            ![
              SoftwareUpgradeTaskType.ROLLBACK_UPGRADE,
              SoftwareUpgradeTaskType.SOFTWARE_UPGRADE
            ].includes(failedTask?.type) && (
              <RbacValidator
                accessRequiredOn={{
                  onResource: currentUniverse.universeUUID,
                  ...ApiPermissionMap.ROLLBACK_TASKS
                }}
                isControl
              >
                <YBButton
                  btnText={'Rollback Task'}
                  btnClass="btn btn-default view-task-details-btn"
                  onClick={() =>
                    this.rollbackTaskClicked(failedTask?.id, currentUniverse.universeUUID)
                  }
                />
              </RbacValidator>
            )}
        </div>
      );
    }

    return (
      <div className={'universe-status ' + universeStatus.state.className} onClick={(e) => { e.preventDefault(); }}>
        {statusDisplay}
        { showTaskDetails &&
          [UniverseState.PENDING, UniverseState.BAD].includes(universeStatus.state) && (
            <TaskDetailSimpleComp taskUUID={taskToDisplayInDrawer?.id} universeUUID={currentUniverse.universeUUID} />

          )
        }
        {showAlertsBadge && <UniverseAlertBadge universeUUID={currentUniverse.universeUUID} />}
      </div>
    );
  }
}
