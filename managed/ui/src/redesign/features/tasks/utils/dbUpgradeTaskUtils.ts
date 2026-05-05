import { Task, TaskType } from '../dtos';

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
