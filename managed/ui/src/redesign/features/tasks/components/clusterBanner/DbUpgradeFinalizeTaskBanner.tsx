import { useState } from 'react';
import { Trans, useTranslation } from 'react-i18next';
import { useMutation, useQuery } from 'react-query';
import { useDispatch } from 'react-redux';
import { Link as MUILink } from '@material-ui/core';
import { AxiosError } from 'axios';

import { showTaskInDrawer } from '@app/actions/tasks';
import { YBButton } from '@app/redesign/components';
import { ApiPermissionMap } from '@app/redesign/features/rbac/ApiAndUserPermMapping';
import { RbacValidator } from '@app/redesign/features/rbac/common/RbacApiPermValidator';
import { YBA_UNIVERSE_UPGRADE_DOCUMENTATION_URL } from '@app/redesign/features/universe/universe-actions/software-upgrade/constants';
import { dbUpgradeMetadataQueryKey } from '@app/redesign/helpers/api';
import { useRefreshUniverseDetailsCache } from '@app/redesign/helpers/cacheUtils';
import { assertUnreachableCase, handleServerError } from '@app/utils/errorHandlingUtils';
import { precheckSoftwareUpgrade } from '@app/v2/api/universe/universe';
import { Task, TaskState } from '../../dtos';
import { getIsDbUpgradeFinalizeTask } from '../../TaskUtils';
import { retryTasks } from '../drawerComp/api';
import { RetryConfirmModal } from '../drawerComp/TaskDetailActions';
import { ClusterOperationBanner, ClusterOperationBannerType } from './ClusterOperationBanner';

interface DbUpgradeFinalizeTaskBannerProps {
  task: Task;
  universeUuid: string;
}

const BANNER_TEST_ID = 'db-upgrade-finalize-task-banner';

export const DbUpgradeFinalizeTaskBanner = ({
  task,
  universeUuid
}: DbUpgradeFinalizeTaskBannerProps) => {
  const [isRetryConfirmModalOpen, setIsRetryConfirmModalOpen] = useState(false);
  const dispatch = useDispatch();
  const refreshUniverseDetailsCache = useRefreshUniverseDetailsCache(universeUuid);
  const { t } = useTranslation('translation', {
    keyPrefix: 'universeActions.dbUpgrade.clusterBanner'
  });
  const targetDbVersion = task.details?.versionNumbers?.ybSoftwareVersion ?? '';

  const dbUpgradeMetadataQuery = useQuery(
    dbUpgradeMetadataQueryKey.detail(universeUuid, {
      yb_software_version: targetDbVersion
    }),
    () =>
      precheckSoftwareUpgrade(universeUuid, {
        yb_software_version: targetDbVersion
      }),
    {
      enabled: !!targetDbVersion && getIsDbUpgradeFinalizeTask(task)
    }
  );

  const retryTaskRbacAccessRequiredOn = {
    onResource: task.targetUUID,
    ...ApiPermissionMap.RETRY_TASKS
  };

  const retryTaskMutation = useMutation(() => retryTasks(task.id), {
    onSuccess: () => {
      refreshUniverseDetailsCache();
      setIsRetryConfirmModalOpen(false);
    },
    onError: (error: Error | AxiosError) => {
      handleServerError(error, {
        customErrorLabel: t('dbUpgradeFinalizeRequestFailedLabel', { keyPrefix: 'toast' })
      });
    }
  });

  if (!getIsDbUpgradeFinalizeTask(task)) {
    return null;
  }

  const { ysql_major_version_upgrade: isYsqlMajorUpgrade = false } =
    dbUpgradeMetadataQuery.data ?? {};

  const openFinalizeTaskDetailsButton = (
    <YBButton
      variant="secondary"
      size="medium"
      data-testid={`${BANNER_TEST_ID}-view-details-button`}
      onClick={() => dispatch(showTaskInDrawer(task.id))}
    >
      {t('actions.viewDetails')}
    </YBButton>
  );

  let bannerComponent = null;

  switch (task.status) {
    case TaskState.RUNNING:
      bannerComponent = (
        <ClusterOperationBanner
          type={ClusterOperationBannerType.IN_PROGRESS}
          title={t('finalizing.title')}
          progressPercent={task.percentComplete ?? 0}
          actions={openFinalizeTaskDetailsButton}
          description={
            <Trans
              t={t}
              i18nKey={
                isYsqlMajorUpgrade
                  ? 'finalizing.descriptionMajorDbUpgrade'
                  : 'finalizing.description'
              }
              components={{
                learnMoreLink: (
                  <MUILink
                    href={YBA_UNIVERSE_UPGRADE_DOCUMENTATION_URL}
                    target="_blank"
                    rel="noopener noreferrer"
                    underline="always"
                  />
                )
              }}
            />
          }
        />
      );
      break;
    case TaskState.FAILURE:
      bannerComponent = (
        <ClusterOperationBanner
          type={ClusterOperationBannerType.ERROR}
          title={t('finalizeFailed.title')}
          progressPercent={task.percentComplete ?? 0}
          actions={
            <>
              {openFinalizeTaskDetailsButton}
              {task.retryable && (
                <RbacValidator accessRequiredOn={retryTaskRbacAccessRequiredOn} isControl>
                  <YBButton
                    variant="secondary"
                    size="medium"
                    data-testid={`${BANNER_TEST_ID}-retry-button`}
                    onClick={() => setIsRetryConfirmModalOpen(true)}
                    showSpinner={retryTaskMutation.isLoading}
                    disabled={retryTaskMutation.isLoading}
                  >
                    {t('actions.retry')}
                  </YBButton>
                </RbacValidator>
              )}
            </>
          }
        />
      );
      break;
    case TaskState.SUCCESS:
    case TaskState.CREATED:
    case TaskState.INITIALIZING:
    case TaskState.PAUSED:
    case TaskState.ABORTED:
    case TaskState.ABORT:
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
        onSubmit={() => retryTaskMutation.mutate()}
      />
    </>
  );
};
