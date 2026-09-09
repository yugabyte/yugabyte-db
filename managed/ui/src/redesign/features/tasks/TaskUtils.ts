/*
 * Created on Wed May 15 2024
 *
 * Copyright 2021 YugabyteDB, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { cloneElement } from 'react';
import { useDispatch, useSelector } from 'react-redux';
import {
  fetchCustomerTasks,
  fetchCustomerTasksFailure,
  fetchCustomerTasksSuccess,
  showTaskInDrawer
} from '../../../actions/tasks';
import { TargetType, Task, TaskState, TaskType } from './dtos';
import { AuditLogProps, DiffApiResp } from './components/diffComp/dtos';
import {
  SoftwareUpgradeState,
  SoftwareUpgradeTaskType
} from '../../../components/universes/helpers/universeHelpers';
import { IUniverse } from '../../../components/backupv2';

/**
 * Checks if a task is currently running.
 * @param task - The task object to check.
 * @returns A boolean indicating whether the task is running or not.
 */
export const isTaskRunning = (task: Task): boolean => {
  return [TaskState.RUNNING, TaskState.INITIALIZING, TaskState.RUNNING, TaskState.ABORT].includes(
    task.status
  );
};

/**
 * Checks if a task has failed.
 * @param task - The task object to check.
 * @returns A boolean indicating whether the task has failed or not.
 */
export const isTaskFailed = (task: Task): boolean =>
  [TaskState.FAILURE, TaskState.ABORTED].includes(task.status);

/**
 * Checks if a task supports before and after data.
 * @param task - The task object to check.
 * @returns A boolean indicating whether the task supports before/after data or not.
 */
export const doesTaskSupportsDiffData = (task: Task): boolean => {
  if (task.type === TaskType.EDIT) {
    return task.target === 'Universe';
  }
  return [TaskType.GFlags_UPGRADE, TaskType.SOFTWARE_UPGRADE, TaskType.RESIZE_NODE].includes(
    task.type
  );
};

/**
 * Custom hook to check if the new task details UI is enabled.
 * @returns A boolean indicating whether the new task details UI is enabled or not.
 */
export function useIsTaskNewUIEnabled(): boolean {
  const featureFlags = useSelector((state: any) => state.featureFlags);
  return featureFlags?.test?.newTaskDetailsUI || featureFlags?.release?.newTaskDetailsUI;
}

export const mapAuditLogToTaskDiffApiResp = (
  auditLog: AuditLogProps | undefined
): DiffApiResp | undefined => {
  if (!auditLog) return undefined;
  return {
    afterData: auditLog.payload,
    beforeData: auditLog.additionalDetails,
    parentUuid: auditLog.taskUUID,
    uuid: auditLog.auditID.toString()
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
  return (
    [SoftwareUpgradeTaskType.ROLLBACK_UPGRADE, SoftwareUpgradeTaskType.SOFTWARE_UPGRADE].includes(
      task.type
    ) &&
    [SoftwareUpgradeState.ROLLBACK_FAILED, SoftwareUpgradeState.UPGRADE_FAILED].includes(
      universe?.universeDetails.softwareUpgradeState
    )
  );
};

// for prechecks , display task typename and target
// for other tasks, display task title
export const getTaskTitle = (task: Task) => {
  return task.typeName.includes('Validation')
    ? `${task.typeName} : ${task.title.split(':')?.[1]}`
    : task.title;
};

export const getErrorTaskTitle = (task: Task) => {
  return `${task.typeName} ${task.target} failed: ${task.title.split(':')?.[1]}`;
};

export const getLatestUniverseTask = (
  customerTaskList: Task[] | undefined | null,
  universeUuid: string
): Task | undefined => {
  return (customerTaskList ?? []).reduce((latestTask: Task | undefined, task: Task) => {
    if (task.targetUUID !== universeUuid) {
      return latestTask;
    }
    if (!latestTask) {
      return task;
    }
    const taskTime = Date.parse(task.createTime);
    const latestTime = Date.parse(latestTask.createTime);
    if (Number.isNaN(taskTime) && Number.isNaN(latestTime)) {
      return latestTask;
    }
    if (Number.isNaN(taskTime)) {
      return latestTask;
    }
    if (Number.isNaN(latestTime)) {
      return task;
    }
    return taskTime > latestTime ? task : latestTask;
  }, undefined);
};

/** Latest `SOFTWARE_UPGRADE` task for the universe by `createTime` (matches paged filter `typeList`). */
export const getLatestSoftwareUpgradeTaskForUniverse = (
  customerTaskList: Task[] | undefined | null,
  universeUuid: string
): Task | undefined => {
  return (customerTaskList ?? []).reduce((latestTask: Task | undefined, task: Task) => {
    if (task.targetUUID !== universeUuid || task.type !== TaskType.SOFTWARE_UPGRADE) {
      return latestTask;
    }
    if (!latestTask) {
      return task;
    }
    const taskTime = Date.parse(task.createTime);
    const latestTime = Date.parse(latestTask.createTime);
    if (Number.isNaN(taskTime) && Number.isNaN(latestTime)) {
      return latestTask;
    }
    if (Number.isNaN(taskTime)) {
      return latestTask;
    }
    if (Number.isNaN(latestTime)) {
      return task;
    }
    return taskTime > latestTime ? task : latestTask;
  }, undefined);
};

/**
 * Prefix that the backend prepends to `CustomerTaskFormData.typeName` when an
 * upgrade-family task is submitted with `UniverseTaskParams.runOnlyPrechecks = true`.
 *
 * Source of truth: `CustomerTaskManager.getCustomTaskName` in
 * `managed/src/main/java/com/yugabyte/yw/common/CustomerTaskManager.java` —
 * which returns `"Validation " + baseName` for precheck-only runs. This is the
 * only precheck signal surfaced on the paginated task response today
 * (`taskInfo.taskParams` is not populated on paged rows).
 *
 * Keep this constant in sync with the backend. The trailing space is
 * intentional so we don't accidentally match an unrelated future type name
 * that happens to start with "Validation".
 *
 * Opened a ticket to have backend report when the current task is a precheck task:
 * https://yugabyte.atlassian.net/browse/PLAT-20601
 */
const PRECHECK_TASK_TYPE_NAME_PREFIX = 'Validation ';

export const getIsPreCheckTask = (task: Task): boolean =>
  task.typeName.startsWith(PRECHECK_TASK_TYPE_NAME_PREFIX);

export const getIsDbUpgradeTask = (task: Task): boolean =>
  task.type === TaskType.SOFTWARE_UPGRADE && !getIsPreCheckTask(task);

export const getIsDbUpgradePrecheckTask = (task: Task): boolean =>
  task.type === TaskType.SOFTWARE_UPGRADE && getIsPreCheckTask(task);

export const getIsDbUpgradeRollbackTask = (task: Task): boolean =>
  task.type === TaskType.ROLLBACK_UPGRADE;

export const getIsDbUpgradeFinalizeTask = (task: Task): boolean =>
  task.type === TaskType.FINALIZE_UPGRADE;

/**
 * Targets whose `targetUUID` is a universe UUID. `Cluster` covers read replica and add-on cluster
 * operations, which the backend still records against `universe.getUniverseUUID()`.
 *
 * Also the targets an edit universe task (`EditUniverse` / `EditKubernetesUniverse`) is recorded
 * against: `Universe` for a primary cluster edit, `Cluster` for a read replica edit.
 */
const UNIVERSE_TASK_TARGETS: TargetType[] = [TargetType.UNIVERSE, TargetType.CLUSTER];

/**
 * Universe this task ran against, or `undefined` when the task targets something else — a backup,
 * provider, or schedule, whose `targetUUID` is that resource's UUID and not a universe.
 *
 * Use this instead of reading `task.targetUUID` directly whenever the UUID is about to be treated
 * as a universe (universe queries, RBAC `onResource`).
 */
export const getTaskUniverseUuid = (task: Task): string | undefined =>
  UNIVERSE_TASK_TARGETS.includes(task.target) ? task.targetUUID : undefined;

/**
 * Non-precheck edit universe, on VM or Kubernetes.
 *
 * Known overlap: `UniverseCRUDHandler.migrateUniverse` records its customer task as `Update` +
 * `Universe`, even though the commissioner task type is `MigrateUniverse`, so a failed migrate
 * also matches this check. Nothing on the customer task payload separates the two today —
 * `typeName` is the `Update` friendly name for both, `title` is `Updated Universe : <name>` for
 * both, and `CustomerTaskFormData.taskInfo` (which would carry the commissioner task type) is
 * never populated by `CustomerTaskHandler.buildCustomerTaskFromData`. Since `MigrateUniverse` is
 * annotated neither `@Retryable` nor `@CanRollback`, a failed migrate gets the edit universe
 * banner copy but no retry or rollback action.
 *
 * The fix belongs on the backend: record the migrate customer task as
 * `CustomerTask.TaskType.MigrateUniverse`, which already exists as an enum value and is already
 * the mapping declared by the `TaskType` registry. This check becomes exact once that lands.
 */
export const getIsEditUniverseTask = (task: Task): boolean =>
  task.type === TaskType.EDIT &&
  UNIVERSE_TASK_TARGETS.includes(task.target) &&
  !getIsPreCheckTask(task);

export const getIsEditUniverseRollbackTask = (task: Task): boolean =>
  task.type === TaskType.ROLLBACK_EDIT_UNIVERSE && UNIVERSE_TASK_TARGETS.includes(task.target);

/** Non-precheck software upgrade, rollback, or finalize — matches DB upgrade cluster banners (excludes precheck-only). */
export const getIsSoftwareUpgradeLockingTask = (task: Task): boolean =>
  getIsDbUpgradeTask(task) || getIsDbUpgradeRollbackTask(task) || getIsDbUpgradeFinalizeTask(task);

/** Latest upgrade / rollback / finalize task for the universe by `createTime` (precheck tasks excluded). */
export const getLatestSoftwareUpgradeLockingTaskForUniverse = (
  customerTaskList: Task[] | undefined | null,
  universeUuid: string
): Task | undefined => {
  return (customerTaskList ?? []).reduce((latestTask: Task | undefined, task: Task) => {
    if (task.targetUUID !== universeUuid || !getIsSoftwareUpgradeLockingTask(task)) {
      return latestTask;
    }
    if (!latestTask) {
      return task;
    }
    const taskTime = Date.parse(task.createTime);
    const latestTime = Date.parse(latestTask.createTime);
    if (Number.isNaN(taskTime) && Number.isNaN(latestTime)) {
      return latestTask;
    }
    if (Number.isNaN(taskTime)) {
      return latestTask;
    }
    if (Number.isNaN(latestTime)) {
      return task;
    }
    return taskTime > latestTime ? task : latestTask;
  }, undefined);
};
