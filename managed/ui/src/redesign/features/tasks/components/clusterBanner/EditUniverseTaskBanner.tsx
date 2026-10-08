/*
 * Copyright 2026 YugabyteDB, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { useState } from 'react';
import { useDispatch } from 'react-redux';
import { useTranslation } from 'react-i18next';
import { Typography, makeStyles } from '@material-ui/core';
import {
  OperationBannerVariant,
  YBButton,
  YBOperationBanner
} from '@yugabyte-ui-library/core';

import { showTaskInDrawer } from '@app/actions/tasks';
import { ApiPermissionMap } from '@app/redesign/features/rbac/ApiAndUserPermMapping';
import { RbacValidator } from '@app/redesign/features/rbac/common/RbacApiPermValidator';
import { YBProgressBarState } from '@app/redesign/components/YBProgress/YBLinearProgress';
import { assertUnreachableCase } from '@app/utils/errorHandlingUtils';
import { useTaskActionMutations } from '../../hooks/useTaskActionMutations';
import { getIsEditUniverseTask } from '../../TaskUtils';
import { Task, TaskState } from '../../dtos';
import { RetryConfirmModal } from '../drawerComp/TaskDetailActions';
import { EditUniverseRollbackConfirmModal } from './EditUniverseRollbackConfirmModal';
import { OperationBannerProgressContent } from './OperationBannerProgressContent';
import { OperationBannerLoadingIcon } from './operationBannerIcons';

import InfoIcon from '@app/redesign/assets/info.svg';

interface EditUniverseTaskBannerProps {
  task: Task;
  universeUuid: string;
  onDismiss: () => void;
}

const BANNER_TEST_ID = 'edit-universe-task-banner';
const TRANSLATION_KEY_PREFIX = 'taskDetails.editUniverseTaskBanner';

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
  },
  rollBackGroup: {
    display: 'flex',
    alignItems: 'center',
    gap: theme.spacing(0.5)
  },
  rollBackHelperText: {
    display: 'flex',
    alignItems: 'center',
    gap: theme.spacing(0.5),

    color: theme.palette.grey[700]
  },
  infoIcon: {
    width: 16,
    height: 16,
    minWidth: 16
  }
}));

/**
 * Banner for an edit universe task (VM or Kubernetes), covering the whole task lifecycle.
 *
 * In progress and success show View Details; failure and abort add retry / roll back, gated on
 * the task's `retryable` / `canRollback` flags. Precheck-only edits are excluded by
 * {@link getIsEditUniverseTask}.
 */
export const EditUniverseTaskBanner = ({
  task,
  universeUuid,
  onDismiss
}: EditUniverseTaskBannerProps) => {
  const [isRetryConfirmModalOpen, setIsRetryConfirmModalOpen] = useState(false);
  const [isRollbackConfirmModalOpen, setIsRollbackConfirmModalOpen] = useState(false);
  const dispatch = useDispatch();
  const classes = useStyles();
  const { t } = useTranslation('translation', {
    keyPrefix: TRANSLATION_KEY_PREFIX
  });
  const { t: tToast } = useTranslation('translation', { keyPrefix: 'toast' });
  const { retryTaskMutation, rollbackTaskMutation } = useTaskActionMutations(
    task.id,
    universeUuid,
    {
      retryCompleted: tToast('retryUniverseUpdateTaskSuccess'),
      rollbackCompleted: tToast('rollbackUniverseUpdateTaskSuccess'),
      retryFailedLabel: tToast('retryUniverseUpdateTaskFailedLabel'),
      rollbackFailedLabel: tToast('rollbackUniverseUpdateTaskFailedLabel')
    }
  );
  const isTaskActionInFlight = retryTaskMutation.isLoading || rollbackTaskMutation.isLoading;

  if (!getIsEditUniverseTask(task)) {
    return null;
  }

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
    // The universe stays locked from submit until the task reaches a terminal state, so every
    // non-terminal state gets the in-progress banner.
    case TaskState.CREATED:
    case TaskState.INITIALIZING:
    case TaskState.RUNNING:
    case TaskState.PAUSED:
    case TaskState.ABORT:
      bannerComponent = (
        <YBOperationBanner
          variant={OperationBannerVariant.Info}
          dense
          minHeight={46}
          iconCircle={false}
          icon={<OperationBannerLoadingIcon />}
          title={t('inProgress.title')}
          message={t('inProgress.description')}
          content={
            <OperationBannerProgressContent progressPercent={task.percentComplete ?? 0} />
          }
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
          dense
          minHeight={46}
          title={t('failed.title')}
          content={
            <OperationBannerProgressContent
              progressPercent={task.percentComplete ?? 0}
              state={YBProgressBarState.Error}
            />
          }
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
                    dataTestId={`${BANNER_TEST_ID}-retry-button`}
                    showSpinner={retryTaskMutation.isLoading}
                    disabled={isTaskActionInFlight}
                    onClick={() => setIsRetryConfirmModalOpen(true)}
                  >
                    {t('actions.retry')}
                  </YBButton>
                </RbacValidator>
              )}
              {task.canRollback && (
                <>
                  <div className={classes.divider} />
                  <div className={classes.rollBackGroup}>
                    <RbacValidator
                      accessRequiredOn={{
                        onResource: universeUuid,
                        ...ApiPermissionMap.ROLLBACK_TASKS
                      }}
                      isControl
                    >
                      <YBButton
                        variant="ghost"
                        size="small"
                        dataTestId={`${BANNER_TEST_ID}-roll-back-button`}
                        showSpinner={rollbackTaskMutation.isLoading}
                        disabled={isTaskActionInFlight}
                        onClick={() => setIsRollbackConfirmModalOpen(true)}
                      >
                        {t('actions.rollBack')}
                      </YBButton>
                    </RbacValidator>
                    <div className={classes.rollBackHelperText}>
                      <InfoIcon className={classes.infoIcon} />
                      <Typography variant="subtitle1" component="span" color="inherit">
                        {t('actions.rollBackHelperText')}
                      </Typography>
                    </div>
                  </div>
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

  return (
    <>
      {bannerComponent}
      <RetryConfirmModal
        visible={isRetryConfirmModalOpen}
        onClose={() => setIsRetryConfirmModalOpen(false)}
        onSubmit={() =>
          retryTaskMutation.mutate(undefined, {
            onSettled: () => setIsRetryConfirmModalOpen(false)
          })
        }
      />
      <EditUniverseRollbackConfirmModal
        visible={isRollbackConfirmModalOpen}
        onClose={() => setIsRollbackConfirmModalOpen(false)}
        onSubmit={() =>
          rollbackTaskMutation.mutate(undefined, {
            onSettled: () => setIsRollbackConfirmModalOpen(false)
          })
        }
      />
    </>
  );
};
