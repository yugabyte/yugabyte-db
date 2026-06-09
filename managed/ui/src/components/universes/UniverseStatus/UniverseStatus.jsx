// Copyright (c) YugabyteDB, Inc.

import { Component } from 'react';
import _ from 'lodash';
import { browserHistory } from 'react-router';

import { isNonEmptyObject } from '../../../utils/ObjectUtils';
import { YBButton } from '../../common/forms/fields';
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
import { handleServerError } from '../../../utils/errorHandlingUtils';
import { api } from '../../../redesign/helpers/api';
import {
  getIsDbUpgradeTask,
  getIsDbUpgradeFinalizeTask,
  getIsDbUpgradeRollbackTask,
  getIsDbUpgradePrecheckTask,
  getLatestUniverseTask
} from '../../../redesign/features/tasks/TaskUtils';

//icons
import AlertIcon from '../../../redesign/assets/approved/alert.svg';
import SuccessIcon from '../../../redesign/assets/approved/success.svg';
import PendingIcon from '../../../redesign/assets/approved/pending.svg';
import PausedIcon from '../../../redesign/assets/approved/paused.svg';
import ErrorIcon from '../../../redesign/assets/approved/error.svg';
import LoadingIcon from '../../../redesign/assets/default-loading-circles.svg';

import './UniverseStatus.scss';

export default class UniverseStatus extends Component {
  componentDidUpdate(prevProps) {
    const { currentUniverse, refreshUniverseData } = this.props;
    const universeUUID = currentUniverse?.universeUUID;
    const customerTaskList = this.props.tasks?.customerTaskList ?? [];
    const prevCustomerTaskList = prevProps.tasks?.customerTaskList ?? [];

    if (
      universeUUID &&
      currentUniverse?.universeDetails?.updateInProgress &&
      !hasPendingTasksForUniverse(universeUUID, customerTaskList) &&
      hasPendingTasksForUniverse(universeUUID, prevCustomerTaskList)
    ) {
      refreshUniverseData();
    }
  }

  retryTaskClicked = (currentTaskUUID, universeUUID) => {
    api
      .retryTask(currentTaskUUID)
      .then(() => {
        browserHistory.push(`/universes/${universeUUID}/tasks`);
      })
      .catch((error) => {
        handleServerError(error, { customErrorLabel: 'Retry Task Failed' });
      });
  };

  rollbackTaskClicked = (currentTaskUUID, universeUUID) => {
    api
      .rollbackTask(currentTaskUUID)
      .then(() => {
        browserHistory.push(`/universes/${universeUUID}/tasks`);
      })
      .catch((error) => {
        handleServerError(error, { customErrorLabel: 'Rollback Task Failed' });
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
      tasks,
      showAlertsBadge,
      alertBadgeListView,
      shouldDisplayTaskButton,
      runtimeConfigs,
      featureFlags,
      showTaskDetailsInDrawer,
      showTaskDetails = false
    } = this.props;
    const customerTaskList = tasks?.customerTaskList ?? [];

    const isRollBackFeatEnabled =
      runtimeConfigs?.data?.configEntries?.find(
        (c) => c.key === 'yb.upgrade.enable_rollback_support'
      )?.value === 'true';

    const universeStatus = getUniverseStatus(currentUniverse);
    const universePendingTask = getUniversePendingTask(
      currentUniverse.universeUUID,
      customerTaskList
    );
    const latestUniverseTask = getLatestUniverseTask(
      customerTaskList,
      currentUniverse.universeUUID
    );
    let taskToDisplayInDrawer = universePendingTask;
    const isNewTaskDetailsUIEnabled =
      featureFlags.test.newTaskDetailsUI || featureFlags.released.newTaskDetailsUI;

    const universeUpgradeState = _.get(currentUniverse, 'universeDetails.softwareUpgradeState');
    let statusDisplay = (
      <div className="status-container pending">
        <LoadingIcon width={24} height={24} />
        {showLabelText && <span>{universeStatus.state.text}</span>}
      </div>
    );

    if (universeStatus.state === UniverseState.GOOD) {
      const isLatestUniverseTaskFailedOrAborted =
        latestUniverseTask &&
        (latestUniverseTask.status === 'Failure' || latestUniverseTask.status === 'Aborted');
      statusDisplay = (
        <div className="status-container good">
          <SuccessIcon width={24} height={24} />
          {showLabelText && universeStatus.state.text && <span>{universeStatus.state.text}</span>}
        </div>
      );

      if (universeUpgradeState === SoftwareUpgradeState.PRE_FINALIZE) {
        statusDisplay = (
          <div className="status-container warning">
            <PendingIcon width={24} height={24} />
            {showLabelText && <span>Pending upgrade finalization</span>}
          </div>
        );
      }
      if (
        latestUniverseTask &&
        latestUniverseTask.status === 'Running' &&
        getIsDbUpgradePrecheckTask(latestUniverseTask)
      ) {
        statusDisplay = (
          <div className="status-container pending">
            <LoadingIcon width={24} height={24} />
            {showLabelText && <span>DB upgrade pre-check in progress...</span>}
          </div>
        );
      }
      if (
        latestUniverseTask &&
        isLatestUniverseTaskFailedOrAborted &&
        getIsDbUpgradePrecheckTask(latestUniverseTask)
      ) {
        statusDisplay = (
          <div className="status-container warning">
            <AlertIcon width={24} height={24} />
            {showLabelText && universeStatus.state.text && <span>DB upgrade pre-check failed</span>}
          </div>
        );
      }
      if (
        latestUniverseTask &&
        isLatestUniverseTaskFailedOrAborted &&
        getIsDbUpgradeTask(latestUniverseTask)
      ) {
        statusDisplay = (
          <div className="status-container warning">
            <AlertIcon width={24} height={24} />
            {showLabelText && <span>DB upgrade aborted</span>}
          </div>
        );
      }
    } else if (universeStatus.state === UniverseState.PAUSED) {
      statusDisplay = (
        <div className="status-container paused">
          <PausedIcon width={24} height={24} />
          {showLabelText && universeStatus.state.text && <span>{universeStatus.state.text}</span>}
        </div>
      );
    } else if (
      universeStatus.state === UniverseState.PENDING &&
      isNonEmptyObject(universePendingTask)
    ) {
      const pendingTaskLabel = getIsDbUpgradeTask(universePendingTask)
        ? 'DB upgrade in progress'
        : getIsDbUpgradeRollbackTask(universePendingTask)
          ? 'DB rollback in progress'
          : getIsDbUpgradeFinalizeTask(universePendingTask)
            ? 'DB upgrade finalize in progress'
            : universeStatus.state.text;
      statusDisplay = (
        <div className="status-container pending">
          <LoadingIcon width={24} height={24} />
          {showLabelText && (
            <span>{`${pendingTaskLabel}... (${universePendingTask.percentComplete}%)`}</span>
          )}
        </div>
      );
    } else if (
      universeStatus.state === UniverseState.BAD ||
      universeStatus.state === UniverseState.WARNING
    ) {
      const failedTask = getcurrentUniverseFailedTask(currentUniverse, customerTaskList);
      taskToDisplayInDrawer = failedTask;

      if (
        latestUniverseTask &&
        getIsDbUpgradeTask(latestUniverseTask) &&
        universeUpgradeState === SoftwareUpgradeState.PAUSED
      ) {
        statusDisplay = (
          <div className="status-container warning">
            <PendingIcon width={24} height={24} />
            {showLabelText && <span>DB upgrade paused</span>}
          </div>
        );
      } else if (failedTask && getIsDbUpgradeTask(failedTask)) {
        statusDisplay = (
          <div className="status-container error">
            <ErrorIcon width={24} height={24} />
            {showLabelText && <span>DB upgrade failed</span>}
          </div>
        );
      } else if (failedTask && getIsDbUpgradeRollbackTask(failedTask)) {
        statusDisplay = (
          <div className="status-container error">
            <ErrorIcon width={24} height={24} />
            {showLabelText && <span>DB rollback failed</span>}
          </div>
        );
      } else if (failedTask && getIsDbUpgradeFinalizeTask(failedTask)) {
        statusDisplay = (
          <div className="status-container error">
            <ErrorIcon width={24} height={24} />
            {showLabelText && <span>DB upgrade finalize failed</span>}
          </div>
        );
      } else {
        statusDisplay = (
          <div className={showLabelText ? 'status-error' : ''}>
            <ErrorIcon width={24} height={24} />
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
            {showLabelText &&
              shouldDisplayTaskButton &&
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
                    isNewTaskDetailsUIEnabled
                      ? showTaskDetailsInDrawer(failedTask?.id)
                      : this.redirectToTaskLogs(failedTask?.id, currentUniverse.universeUUID)
                  }
                />
              )}
            {showLabelText &&
              shouldDisplayTaskButton &&
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
            {showLabelText &&
              shouldDisplayTaskButton &&
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
    }

    return (
      <div
        className={'universe-status ' + universeStatus.state.className}
        onClick={(e) => {
          e.preventDefault();
        }}
      >
        {statusDisplay}
        {showTaskDetails &&
          [UniverseState.PENDING, UniverseState.BAD].includes(universeStatus.state) && (
            <TaskDetailSimpleComp
              taskUUID={taskToDisplayInDrawer?.id}
              universeUUID={currentUniverse.universeUUID}
            />
          )}
        {showAlertsBadge && (
          <UniverseAlertBadge
            universeUUID={currentUniverse.universeUUID}
            listView={alertBadgeListView}
          />
        )}
      </div>
    );
  }
}
