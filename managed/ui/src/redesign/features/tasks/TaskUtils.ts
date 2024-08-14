/*
 * Created on Wed May 15 2024
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { useSelector } from 'react-redux';
import { Task, TaskStates, TaskType } from './dtos';

/**
 * Checks if a task is currently running.
 * @param task - The task object to check.
 * @returns A boolean indicating whether the task is running or not.
 */
export const isTaskRunning = (task: Task): boolean => {
  return [TaskStates.RUNNING, TaskStates.INITIALIZING, TaskStates.RUNNING].includes(task.status);
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
