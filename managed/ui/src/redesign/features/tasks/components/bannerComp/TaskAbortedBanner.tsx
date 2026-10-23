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

import { YBProgressBarState } from '@app/redesign/components/YBProgress/YBLinearProgress';
import { ApiPermissionMap } from '@app/redesign/features/rbac/ApiAndUserPermMapping';
import { RbacValidator } from '@app/redesign/features/rbac/common/RbacApiPermValidator';
import { getAbortedTaskTitle, getTaskUniverseUuid } from '../../TaskUtils';
import { Task } from '../../dtos';
import { useTaskActionMutations } from '../../hooks/useTaskActionMutations';
import { RetryConfirmModal } from '../drawerComp/TaskDetailActions';
import { OperationBannerProgressContent } from '../clusterBanner/OperationBannerProgressContent';

interface TaskAbortedBannerProps {
  currentTask: Task;
  viewDetails: () => void;
}

const BANNER_TEST_ID = 'task-aborted-banner';

const useStyles = makeStyles((theme) => ({
  actions: {
    display: 'flex',
    alignItems: 'center',
    gap: theme.spacing(1)
  }
}));

export const TaskAbortedBanner = ({ currentTask, viewDetails }: TaskAbortedBannerProps) => {
  const [isRetryConfirmModalOpen, setIsRetryConfirmModalOpen] = useState(false);
  const classes = useStyles();
  const { t } = useTranslation('translation', { keyPrefix: 'taskDetails' });
  const taskUniverseUuid = getTaskUniverseUuid(currentTask);
  const { retryTaskMutation } = useTaskActionMutations(currentTask.id, taskUniverseUuid);

  return (
    <>
      <YBOperationBanner
        variant={OperationBannerVariant.Error}
        dense
        minHeight={46}
        showDivider={false}
        title={getAbortedTaskTitle(currentTask)}
        message={t('banner.abortedDescription')}
        content={
          <OperationBannerProgressContent
            progressPercent={currentTask.percentComplete ?? 0}
            state={YBProgressBarState.Error}
          />
        }
        dataTestId={BANNER_TEST_ID}
        action={
          <div className={classes.actions}>
            <YBButton
              variant="secondary"
              size="small"
              dataTestId={`${BANNER_TEST_ID}-view-details-button`}
              onClick={viewDetails}
            >
              {t('banner.viewDetails')}
            </YBButton>
            {currentTask.retryable && (
              <RbacValidator
                accessRequiredOn={{
                  onResource: taskUniverseUuid,
                  ...ApiPermissionMap.RETRY_TASKS
                }}
                isControl
              >
                <YBButton
                  variant="secondary"
                  size="small"
                  dataTestId={`${BANNER_TEST_ID}-retry-button`}
                  showSpinner={retryTaskMutation.isLoading}
                  disabled={retryTaskMutation.isLoading}
                  onClick={() => setIsRetryConfirmModalOpen(true)}
                >
                  {t('actions.retry')}
                </YBButton>
              </RbacValidator>
            )}
          </div>
        }
      />
      <RetryConfirmModal
        visible={isRetryConfirmModalOpen}
        onClose={() => setIsRetryConfirmModalOpen(false)}
        onSubmit={() =>
          retryTaskMutation.mutate(undefined, {
            onSettled: () => setIsRetryConfirmModalOpen(false)
          })
        }
      />
    </>
  );
};
