import { useState } from 'react';
import { Trans, useTranslation } from 'react-i18next';
import { useMutation, useQuery } from 'react-query';
import { useDispatch } from 'react-redux';
import { Link as MUILink } from '@material-ui/core';
import { AxiosError } from 'axios';
import { OperationBannerVariant, YBOperationBanner } from '@yugabyte-ui-library/core';

import { showTaskInDrawer } from '@app/actions/tasks';
import { YBButton } from '@app/redesign/components';
import { YBProgressBarState } from '@app/redesign/components/YBProgress/YBLinearProgress';
import { ApiPermissionMap } from '@app/redesign/features/rbac/ApiAndUserPermMapping';
import { RbacValidator } from '@app/redesign/features/rbac/common/RbacApiPermValidator';
import { YBA_UNIVERSE_UPGRADE_DOCUMENTATION_URL } from '@app/redesign/features/universe/universe-actions/software-upgrade/constants';
import { dbUpgradeMetadataQueryKey } from '@app/redesign/helpers/api';
import { useRefreshSoftwareUpgradeTasksCache } from '@app/redesign/helpers/cacheUtils';
import { assertUnreachableCase, handleServerError } from '@app/utils/errorHandlingUtils';
import { retryTask } from '@app/v2/api/task/task';
import { precheckSoftwareUpgrade } from '@app/v2/api/universe/universe';
import { Task, TaskState } from '../../dtos';
import { getIsDbUpgradeRollbackTask } from '../../TaskUtils';
import { RetryConfirmModal } from '../drawerComp/TaskDetailActions';
import { OperationBannerProgressContent } from './OperationBannerProgressContent';
import { OperationBannerLoadingIcon } from './operationBannerIcons';

interface DbUpgradeRollbackTaskBannerProps {
  task: Task;
  universeUuid: string;
}

const BANNER_TEST_ID = 'db-upgrade-rollback-task-banner';

export const DbUpgradeRollbackTaskBanner = ({
  task,
  universeUuid
}: DbUpgradeRollbackTaskBannerProps) => {
  const [isRetryConfirmModalOpen, setIsRetryConfirmModalOpen] = useState(false);
  const dispatch = useDispatch();
  const refreshSoftwareUpgradeTasksCache = useRefreshSoftwareUpgradeTasksCache(universeUuid);
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
      enabled: !!targetDbVersion && getIsDbUpgradeRollbackTask(task)
    }
  );

  const retryTaskRbacAccessRequiredOn = {
    onResource: task.targetUUID,
    ...ApiPermissionMap.RETRY_TASKS
  };

  const retryTaskMutation = useMutation(() => retryTask(task.id, {}), {
    onError: (error: Error | AxiosError) => {
      handleServerError(error, {
        customErrorLabel: t('dbUpgradeRollbackRequestFailedLabel', { keyPrefix: 'toast' })
      });
    },
    onSettled: () => {
      setIsRetryConfirmModalOpen(false);
      refreshSoftwareUpgradeTasksCache();
    }
  });

  if (!getIsDbUpgradeRollbackTask(task)) {
    return null;
  }

  const { ysql_major_version_upgrade: isYsqlMajorUpgrade = false } =
    dbUpgradeMetadataQuery.data ?? {};

  const openRollbackTaskDetailsButton = (
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
        <YBOperationBanner
          variant={OperationBannerVariant.Info}
          dense
          minHeight={46}
          showDivider={false}
          iconCircle={false}
          icon={<OperationBannerLoadingIcon />}
          title={t('rollingBack.title')}
          content={
            <OperationBannerProgressContent progressPercent={task.percentComplete ?? 0} />
          }
          action={openRollbackTaskDetailsButton}
          message={
            <Trans
              t={t}
              i18nKey={
                isYsqlMajorUpgrade
                  ? 'rollingBack.descriptionMajorDbUpgrade'
                  : 'rollingBack.description'
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
        <YBOperationBanner
          variant={OperationBannerVariant.Error}
          dense
          minHeight={46}
          showDivider={false}
          title={t('rollbackFailed.title')}
          content={
            <OperationBannerProgressContent
              progressPercent={task.percentComplete ?? 0}
              state={YBProgressBarState.Error}
            />
          }
          action={
            <>
              {openRollbackTaskDetailsButton}
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
