/*
 * Copyright 2026 YugabyteDB, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { useState } from 'react';

import { makeStyles } from '@material-ui/core';
import { OperationBannerVariant, YBButton, YBOperationBanner } from '@yugabyte-ui-library/core';
import { useTranslation } from 'react-i18next';
import { useDispatch } from 'react-redux';
import { useQuery } from 'react-query';

import { showTaskInDrawer } from '@app/actions/tasks';
import { TASK_SHORT_TIMEOUT } from '@app/components/tasks/constants';
import { ApiPermissionMap } from '@app/redesign/features/rbac/ApiAndUserPermMapping';
import { RbacValidator } from '@app/redesign/features/rbac/common/RbacApiPermValidator';
import { api, taskQueryKey } from '@app/redesign/helpers/api';
import { assertUnreachableCase } from '@app/utils/errorHandlingUtils';
import { useTaskActionMutations } from '../../hooks/useTaskActionMutations';
import { getIsEditUniverseRollbackTask } from '../../TaskUtils';
import { Task, TaskState } from '../../dtos';
import { RetryConfirmModal } from '../drawerComp/TaskDetailActions';
import { OperationBannerProgressContent } from './OperationBannerProgressContent';
import { OperationBannerLoadingIcon } from './operationBannerIcons';

interface EditUniverseRollbackTaskBannerProps {
  task: Task;
  universeUuid: string;
}

const BANNER_TEST_ID = 'edit-universe-rollback-task-banner';
const TRANSLATION_KEY_PREFIX = 'taskDetails.editUniverseRollbackTaskBanner';

const RetryAction = {
  ROLLBACK: 'rollback',
  UNIVERSE_UPDATE: 'universeUpdate'
} as const;
type RetryAction = (typeof RetryAction)[keyof typeof RetryAction];

const useStyles = makeStyles((theme) => ({
  actions: {
    display: 'flex',
    alignItems: 'center',
    gap: theme.spacing(2)
  },
  divider: {
    width: 1,
    height: 24,

    backgroundColor: theme.palette.grey[300]
  }
}));

export const EditUniverseRollbackTaskBanner = ({
  task,
  universeUuid
}: EditUniverseRollbackTaskBannerProps) => {
  const [retryAction, setRetryAction] = useState<RetryAction | null>(null);
  const dispatch = useDispatch();
  const classes = useStyles();
  const { t } = useTranslation('translation', {
    keyPrefix: TRANSLATION_KEY_PREFIX
  });
  const { t: tToast } = useTranslation('translation', { keyPrefix: 'toast' });

  const { retryTaskMutation: retryRollbackMutation } = useTaskActionMutations(
    task.id,
    universeUuid,
    {
      retryCompleted: tToast('rollbackUniverseUpdateTaskSuccess'),
      retryFailedLabel: tToast('retryUniverseRollbackTaskFailedLabel')
    }
  );
  const { retryTaskMutation: retryUniverseUpdateMutation } = useTaskActionMutations(
    task.originalTaskUUID ?? task.id,
    universeUuid,
    {
      retryCompleted: tToast('retryUniverseUpdateTaskSuccess'),
      retryFailedLabel: tToast('retryUniverseUpdateTaskFailedLabel')
    }
  );

  const isTaskFailed = task.status === TaskState.FAILURE || task.status === TaskState.ABORTED;
  // Rollback prechecks run before the universe is frozen, so placement ownership - and with it the
  // failed edit's retryability - only moves to the rollback task once the rollback gets past them.
  const originalEditTaskQuery = useQuery(
    taskQueryKey.detail(task.originalTaskUUID ?? ''),
    () => api.fetchTaskStatus(task.originalTaskUUID!),
    {
      enabled: isTaskFailed && !!task.originalTaskUUID,
      refetchInterval: TASK_SHORT_TIMEOUT
    }
  );

  if (!getIsEditUniverseRollbackTask(task)) {
    return null;
  }

  // Wait for the failed edit before rendering, so the failure copy and actions don't flip once
  // its retryability lands.
  if (originalEditTaskQuery.isLoading) {
    return null;
  }

  const isPrecheckFailure = !!originalEditTaskQuery.data?.retryable;
  const isTaskActionInFlight =
    retryRollbackMutation.isLoading || retryUniverseUpdateMutation.isLoading;
  const viewDetailsButton = (
    <YBButton
      variant="secondary"
      size="small"
      dataTestId={`${BANNER_TEST_ID}-view-details-button`}
      onClick={() => dispatch(showTaskInDrawer(task.id))}
    >
      {t('actions.viewDetails')}
    </YBButton>
  );

  let bannerComponent = null;

  switch (task.status) {
    case TaskState.CREATED:
    case TaskState.INITIALIZING:
    case TaskState.RUNNING:
    case TaskState.PAUSED:
    case TaskState.ABORT:
      bannerComponent = (
        <YBOperationBanner
          variant={OperationBannerVariant.Info}
          icon={<OperationBannerLoadingIcon />}
          title={t('inProgress.title')}
          message={t('inProgress.description')}
          content={<OperationBannerProgressContent progressPercent={task.percentComplete ?? 0} />}
          dataTestId={BANNER_TEST_ID}
          action={viewDetailsButton}
        />
      );
      break;
    case TaskState.FAILURE:
    case TaskState.ABORTED:
      bannerComponent = (
        <YBOperationBanner
          variant={OperationBannerVariant.Error}
          title={t(isPrecheckFailure ? 'precheckFailed.title' : 'failed.title')}
          description={t(isPrecheckFailure ? 'precheckFailed.description' : 'failed.description')}
          dataTestId={BANNER_TEST_ID}
          action={
            <div className={classes.actions}>
              {viewDetailsButton}
              {task.retryable && (
                <RbacValidator
                  accessRequiredOn={{
                    onResource: universeUuid,
                    ...ApiPermissionMap.RETRY_TASKS
                  }}
                  isControl
                >
                  <YBButton
                    variant="secondary"
                    size="small"
                    dataTestId={`${BANNER_TEST_ID}-retry-rollback-button`}
                    showSpinner={retryRollbackMutation.isLoading}
                    disabled={isTaskActionInFlight}
                    onClick={() => setRetryAction(RetryAction.ROLLBACK)}
                  >
                    {t('actions.retryRollback')}
                  </YBButton>
                </RbacValidator>
              )}
              {isPrecheckFailure && (
                <>
                  <div className={classes.divider} />
                  <RbacValidator
                    accessRequiredOn={{
                      onResource: universeUuid,
                      ...ApiPermissionMap.RETRY_TASKS
                    }}
                    isControl
                  >
                    <YBButton
                      variant="ghost"
                      size="small"
                      dataTestId={`${BANNER_TEST_ID}-retry-universe-update-button`}
                      showSpinner={retryUniverseUpdateMutation.isLoading}
                      disabled={isTaskActionInFlight}
                      onClick={() => setRetryAction(RetryAction.UNIVERSE_UPDATE)}
                    >
                      {t('actions.retryUniverseUpdate')}
                    </YBButton>
                  </RbacValidator>
                </>
              )}
            </div>
          }
        />
      );
      break;
    case TaskState.SUCCESS:
    case TaskState.UNKNOWN:
      bannerComponent = null;
      break;
    default:
      assertUnreachableCase(task.status);
  }

  const selectedRetryMutation =
    retryAction === RetryAction.UNIVERSE_UPDATE
      ? retryUniverseUpdateMutation
      : retryRollbackMutation;

  return (
    <>
      {bannerComponent}
      <RetryConfirmModal
        visible={retryAction !== null}
        onClose={() => setRetryAction(null)}
        onSubmit={() =>
          selectedRetryMutation.mutate(undefined, {
            onSettled: () => setRetryAction(null)
          })
        }
      />
    </>
  );
};
