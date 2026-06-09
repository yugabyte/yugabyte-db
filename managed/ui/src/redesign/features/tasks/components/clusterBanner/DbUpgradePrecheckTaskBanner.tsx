import { useState } from 'react';
import { Trans, useTranslation } from 'react-i18next';
import { useQuery } from 'react-query';
import { useDispatch } from 'react-redux';

import { Link as MUILink, Typography } from '@material-ui/core';

import { showTaskInDrawer } from '@app/actions/tasks';
import { YBButton } from '@app/redesign/components';
import { YBA_UNIVERSE_UPGRADE_DOCUMENTATION_URL } from '@app/redesign/features/universe/universe-actions/software-upgrade/constants';
import { DbUpgradeModal } from '@app/redesign/features/universe/universe-actions/software-upgrade/DbUpgradeModal';
import { dbUpgradeMetadataQueryKey } from '@app/redesign/helpers/api';
import { useFormatDatetime, YBTimeFormats } from '@app/redesign/helpers/DateUtils';
import { assertUnreachableCase } from '@app/utils/errorHandlingUtils';
import { precheckSoftwareUpgrade } from '@app/v2/api/universe/universe';

import { Task, TaskState } from '../../dtos';
import { getIsDbUpgradePrecheckTask } from '../../TaskUtils';
import { ClusterOperationBanner, ClusterOperationBannerType } from './ClusterOperationBanner';

interface DbUpgradePrecheckTaskBannerProps {
  task: Task;
  universeUuid: string;
  onDismiss: () => void;
}
const BANNER_TEST_ID = 'db-upgrade-precheck-task-banner';

export const DbUpgradePrecheckTaskBanner = ({
  task,
  universeUuid,
  onDismiss
}: DbUpgradePrecheckTaskBannerProps) => {
  const [isDbUpgradeModalOpen, setIsDbUpgradeModalOpen] = useState(false);
  const dispatch = useDispatch();
  const formatDatetime = useFormatDatetime();
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
      enabled: !!targetDbVersion && getIsDbUpgradePrecheckTask(task)
    }
  );

  if (!getIsDbUpgradePrecheckTask(task)) {
    return null;
  }

  const { ysql_major_version_upgrade: isYsqlMajorUpgrade = false } =
    dbUpgradeMetadataQuery.data ?? {};

  const openPrecheckTaskDetailsButton = (
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
  const precheckPassedDateLabel = task.completionTime
    ? formatDatetime(task.completionTime, YBTimeFormats.YB_LONG_DATETIME)
    : '';

  switch (task.status) {
    case TaskState.RUNNING:
      bannerComponent = (
        <ClusterOperationBanner
          type={ClusterOperationBannerType.IN_PROGRESS}
          title={t('precheckInProgress.title')}
          progressPercent={task.percentComplete ?? 0}
          actions={openPrecheckTaskDetailsButton}
          description={
            <Trans
              t={t}
              i18nKey="precheckInProgress.description"
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
    case TaskState.SUCCESS:
      bannerComponent = (
        <ClusterOperationBanner
          type={ClusterOperationBannerType.PENDING_ACTION_WHITE}
          title={
            <Trans
              t={t}
              i18nKey="precheckPassed.title"
              values={{ date: precheckPassedDateLabel }}
              components={{
                dateLabel: <Typography variant="body2" component="span" />
              }}
            />
          }
          description={
            isYsqlMajorUpgrade
              ? t('precheckPassed.descriptionPg15')
              : t('precheckPassed.description')
          }
          actions={
            <YBButton
              variant="secondary"
              size="medium"
              data-testid={`${BANNER_TEST_ID}-upgrade-database-button`}
              onClick={() => setIsDbUpgradeModalOpen(true)}
            >
              {t('actions.upgradeDatabase')}
            </YBButton>
          }
          onDismiss={onDismiss}
        />
      );
      break;
    case TaskState.FAILURE:
      bannerComponent = (
        <ClusterOperationBanner
          type={ClusterOperationBannerType.ALERT}
          title={t('precheckIssuesFound.title')}
          actions={openPrecheckTaskDetailsButton}
          description={
            isYsqlMajorUpgrade
              ? t('precheckIssuesFound.descriptionPg15')
              : t('precheckIssuesFound.description')
          }
          onDismiss={onDismiss}
        />
      );
      break;
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
      {isDbUpgradeModalOpen && (
        <DbUpgradeModal
          universeUuid={universeUuid}
          modalProps={{
            open: isDbUpgradeModalOpen,
            onClose: () => setIsDbUpgradeModalOpen(false)
          }}
        />
      )}
    </>
  );
};
