import moment from 'moment';
import { useQuery } from 'react-query';

import { TASK_SHORT_TIMEOUT } from '@app/components/tasks/constants';
import { Task } from '@app/redesign/features/tasks/dtos';
import { isTaskRunning } from '@app/redesign/features/tasks/TaskUtils';
import { api, taskQueryKey } from '@app/redesign/helpers/api';
import { ExportType } from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';

const TELEMETRY_CONFIG_TASK_TYPES = new Set([
  'ConfigureExportTelemetryConfig',
  'KubernetesConfigureExportTelemetryConfig'
]);

interface TaskDetailsWithModifiedExportTypes {
  modifiedExportTypes?: ExportType[];
  modified_export_types?: ExportType[];
}

function getModifiedExportTypes(task: Task | undefined): ExportType[] {
  const details = task?.details as TaskDetailsWithModifiedExportTypes | undefined;
  return details?.modifiedExportTypes ?? details?.modified_export_types ?? [];
}

function isTelemetryConfigTask(task: Task): boolean {
  return TELEMETRY_CONFIG_TASK_TYPES.has(task.type);
}

function getInProgressTelemetryConfigTask(tasks: Task[] | undefined): Task | undefined {
  return tasks
    ?.filter((task) => isTelemetryConfigTask(task) && isTaskRunning(task))
    .sort((firstTask, secondTask) =>
      moment(secondTask.createTime).isBefore(firstTask.createTime) ? -1 : 1
    )[0];
}

/**
 * Derives telemetry-config task state from the same React Query cache as TaskDetailBanner and
 * UniverseTaskList (`taskQueryKey.universe` + `fetchCustomerTasks`). When the banner shows the
 * configure task as in progress, `details.modifiedExportTypes` drives per-card Configuring status.
 */
export function useExportTelemetryConfigTaskStatus(universeUuid: string) {
  const universeTasksQuery = useQuery(
    taskQueryKey.universe(universeUuid),
    () => api.fetchCustomerTasks(universeUuid),
    {
      enabled: !!universeUuid,
      refetchInterval: TASK_SHORT_TIMEOUT
    }
  );

  const inProgressTelemetryConfigTask = getInProgressTelemetryConfigTask(universeTasksQuery.data);
  const modifiedExportTypes = getModifiedExportTypes(inProgressTelemetryConfigTask);
  const isTelemetryConfigTaskInProgress = !!inProgressTelemetryConfigTask;

  return {
    isTelemetryConfigTaskInProgress,
    isQueryLogConfiguring: modifiedExportTypes.includes(ExportType.QUERY_LOGS),
    isAuditLogConfiguring: modifiedExportTypes.includes(ExportType.AUDIT_LOGS),
    isMetricsExportConfiguring: modifiedExportTypes.includes(ExportType.METRICS)
  };
}
