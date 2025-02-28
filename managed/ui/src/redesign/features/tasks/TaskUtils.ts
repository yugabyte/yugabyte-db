/*
 * Created on Wed May 15 2024
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { cloneElement } from 'react';
import { useDispatch, useSelector } from 'react-redux';
import { fetchCustomerTasks, fetchCustomerTasksFailure, fetchCustomerTasksSuccess, showTaskInDrawer } from '../../../actions/tasks';
import { Task, TaskStates, TaskType } from './dtos';
import { AuditLogProps, DiffApiResp } from './components/diffComp/dtos';
import { SoftwareUpgradeState, SoftwareUpgradeTaskType } from '../../../components/universes/helpers/universeHelpers';
import { IUniverse } from '../../../components/backupv2';

/**
 * Checks if a task is currently running.
 * @param task - The task object to check.
 * @returns A boolean indicating whether the task is running or not.
 */
export const isTaskRunning = (task: Task): boolean => {
  return [TaskStates.RUNNING, TaskStates.INITIALIZING, TaskStates.RUNNING, TaskStates.ABORT].includes(task.status);
};

/**
 * Checks if a task has failed.
 * @param task - The task object to check.
 * @returns A boolean indicating whether the task has failed or not.
 */
export const isTaskFailed = (task: Task): boolean =>
  [TaskStates.FAILURE, TaskStates.ABORTED].includes(task.status);

/**
 * Checks if a task supports before and after data.
 * @param task - The task object to check.
 * @returns A boolean indicating whether the task supports before/after data or not.
 */
export const doesTaskSupportsDiffData = (task: Task): boolean => {
  if (task.type === TaskType.EDIT) {
    return task.target === 'Universe';
  }
  return [TaskType.GFlags_UPGRADE, TaskType.SOFTWARE_UPGRADE].includes(task.type);
};

/**
 * Custom hook to check if the new task details UI is enabled.
 * @returns A boolean indicating whether the new task details UI is enabled or not.
 */
export function useIsTaskNewUIEnabled(): boolean {
  const featureFlags = useSelector((state: any) => state.featureFlags);
  return featureFlags?.test?.newTaskDetailsUI || featureFlags?.release?.newTaskDetailsUI;
}

export const mapAuditLogToTaskDiffApiResp = (auditLog: AuditLogProps | undefined): DiffApiResp | undefined => {
  if (!auditLog) return undefined;
  return {
    afterData: auditLog.payload,
    beforeData: auditLog.additionalDetails,
    parentUuid: auditLog.taskUUID,
    uuid: auditLog.auditID.toString(),
  };
};

// Hijacks the click event on the task link (from backup success msg)
// and shows the task details in the drawer, if new task ui is enabled.
export const useInterceptBackupTaskLinks = (): Function => {

  const isNewTasjUIEnabled = useIsTaskNewUIEnabled();
  const dispatch = useDispatch();

  return (a: JSX.Element) => {

    if (!isNewTasjUIEnabled) return a;

    if (a.type !== 'a') return a;

    const taskURL = a.props.href;
    const taskID = taskURL.split('/').pop();

    if (!taskID) return a;

    return cloneElement(a, {
      onClick: (e: any) => {
        //prevent href navigation
        e.preventDefault();
        e.stopPropagation();
        dispatch(showTaskInDrawer(taskID));
      }
    });
  };
};

// Custom hook to refetch tasks.
export const useRefetchTasks = () => {
  const dispatch = useDispatch();
  return function () {
    return dispatch(fetchCustomerTasks() as any).then((response: any) => {
      if (!response.error) {
        return dispatch(fetchCustomerTasksSuccess(response.payload));
      } else {
        return dispatch(fetchCustomerTasksFailure(response.payload));
      }
    });
  };
};

// Check if the task is a software upgrade task and the universe is in a failed state.
export const isSoftwareUpgradeFailed = (task: Task, universe: IUniverse) => {
  return [SoftwareUpgradeTaskType.ROLLBACK_UPGRADE,
  SoftwareUpgradeTaskType.SOFTWARE_UPGRADE].includes(task.type) && [SoftwareUpgradeState.ROLLBACK_FAILED, SoftwareUpgradeState.UPGRADE_FAILED].includes(
    universe?.universeDetails.softwareUpgradeState
  );
};
