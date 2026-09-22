/*
 * Copyright 2026 YugabyteDB, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { AxiosError } from 'axios';
import { useTranslation } from 'react-i18next';
import { useMutation, useQueryClient } from 'react-query';
import { toast } from 'react-toastify';

import { fetchTaskUntilItCompletes } from '@app/actions/xClusterReplication';
import { api } from '@app/redesign/helpers/api';
import {
  useRefreshCustomerTasks,
  useRefreshUniverseTasksCache
} from '@app/redesign/helpers/cacheUtils';
import { YBPTask } from '@app/redesign/helpers/dtos';
import { handleServerError } from '@app/utils/errorHandlingUtils';
import { rollbackTask as rollbackCustomerTask } from '@app/v2/api/task/task';
import { getGetUniverseQueryKey } from '@app/v2/api/universe/universe';
import type { YBATaskRespResponse } from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';

import { Task } from '../dtos';

interface TaskActionToastMessages {
  /** Toast shown when the retried task completes successfully. */
  retryCompleted?: string;
  /** Toast shown when the rollback task completes successfully. */
  rollbackCompleted?: string;
  /** Toast label used when the retry request is rejected. */
  retryFailedLabel?: string;
  /** Toast label used when the rollback request is rejected. */
  rollbackFailedLabel?: string;
}

/**
 * Retry and rollback mutations for a task.
 *
 * Retry and rollback submit a *new* task and return its UUID, so the mutation resolving only means
 * the request was accepted. The submitted task is then polled to completion and the outcome is
 * toasted, matching how the DB upgrade rollback / finalize modals report their operations.
 *
 * After submit and again on completion, refreshTaskRelatedContext uses
 * `useRefreshUniverseTasksCache` so the customer task list (react-query + Redux, including the
 * success/failure follow-up) is reloaded immediately and universe details are refreshed after a
 * short delay — universe details are not updated the instant the new task is accepted. It also
 * invalidates the generated `getGetUniverseQueryKey` cache used by edit-universe and
 * TaskDetailBanner.
 *
 * `universeUuid` is the universe this task belongs to — pass the id a universe-scoped screen
 * already has rather than `task.targetUUID`, which is a backup / provider / schedule UUID for
 * tasks that don't target a universe. Pass `undefined` (see `getTaskUniverseUuid`) for those, and
 * only the customer task list is refreshed.
 *
 * Pass per-call callbacks (e.g. to close a confirmation modal) through
 * `mutate(undefined, { onSettled })`; they run after the shared handlers.
 */
export const useTaskActionMutations = (
  task: Task,
  universeUuid: string | undefined,
  messages?: TaskActionToastMessages
) => {
  const queryClient = useQueryClient();
  const refreshCustomerTasks = useRefreshCustomerTasks();
  // Hooks can't be called conditionally; the returned callback only runs when universeUuid is set.
  const refreshUniverseTasksCache = useRefreshUniverseTasksCache(universeUuid ?? '');
  const { t } = useTranslation('translation', {
    keyPrefix: 'taskDetails.actions'
  });

  const refreshTaskRelatedContext = () => {
    if (!universeUuid) {
      refreshCustomerTasks();
      return;
    }
    refreshUniverseTasksCache();
    queryClient.invalidateQueries(getGetUniverseQueryKey(universeUuid));
  };

  /**
   * Polls the submitted task and toasts once it completes. Failures are left to the task banners,
   * which show the error alongside a retry / roll back action.
   */
  const pollSubmittedTask = (submittedTaskUuid: string, successMessage: string) => {
    fetchTaskUntilItCompletes(
      submittedTaskUuid,
      (isFailure: boolean) => {
        if (!isFailure) {
          toast.success(successMessage);
        }
        refreshTaskRelatedContext();
      },
      refreshTaskRelatedContext
    );
  };

  const retryTaskMutation = useMutation<YBPTask, Error | AxiosError>(
    () => api.retryTask(task.id),
    {
      onSuccess: (response) => {
        refreshTaskRelatedContext();
        if (response?.taskUUID) {
          pollSubmittedTask(
            response.taskUUID,
            messages?.retryCompleted ?? t('messages.taskRetryCompleted')
          );
        }
      },
      onError: (error) => {
        handleServerError(error, {
          customErrorLabel: messages?.retryFailedLabel ?? t('messages.taskRetryFailed')
        });
      }
    }
  );

  const rollbackTaskMutation = useMutation<YBATaskRespResponse, Error | AxiosError>(
    () => rollbackCustomerTask(task.id, {}),
    {
      onSuccess: (response) => {
        refreshTaskRelatedContext();
        if (response?.task_uuid) {
          pollSubmittedTask(
            response.task_uuid,
            messages?.rollbackCompleted ?? t('messages.taskRollbackCompleted')
          );
        }
      },
      onError: (error) => {
        handleServerError(error, {
          customErrorLabel: messages?.rollbackFailedLabel ?? t('messages.taskRollbackFailed')
        });
      }
    }
  );

  return { retryTaskMutation, rollbackTaskMutation };
};
