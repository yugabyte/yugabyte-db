import { useState } from 'react';
import { Link as MUILink } from '@material-ui/core';
import { Trans, useTranslation } from 'react-i18next';
import { useQuery } from 'react-query';
import { OperationBannerVariant, YBOperationBanner } from '@yugabyte-ui-library/core';

import { YBButton } from '@app/redesign/components';
import { DbUpgradeFinalizeModal } from '@app/redesign/features/universe/universe-actions/software-upgrade/DbUpgradeFinalizeModal';
import { DbUpgradeManagementSidePanel } from '@app/redesign/features/universe/universe-actions/software-upgrade/upgrade-management/DbUpgradeManagementSidePanel';
import { DbUpgradeRollBackModal } from '@app/redesign/features/universe/universe-actions/software-upgrade/DbUpgradeRollBackModal';
import { YBA_UNIVERSE_UPGRADE_DOCUMENTATION_URL } from '@app/redesign/features/universe/universe-actions/software-upgrade/constants';
import { dbUpgradeMetadataQueryKey, universeQueryKey } from '@app/redesign/helpers/api';
import { getUniverse, precheckSoftwareUpgrade } from '@app/v2/api/universe/universe';
import { UniverseInfoSoftwareUpgradeState } from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';
import { formatYbSoftwareVersionString } from '@app/utils/Formatters';
import { assertUnreachableCase } from '@app/utils/errorHandlingUtils';
import { YBProgressBarState } from '@app/redesign/components/YBProgress/YBLinearProgress';
import { PollingIntervalMs } from '@app/components/xcluster/constants';
import { getIsDbUpgradeTask } from '../../TaskUtils';
import { Task, TaskState } from '../../dtos';
import { OperationBannerProgressContent } from './OperationBannerProgressContent';
import { OperationBannerLoadingIcon, OperationBannerWaveIcon } from './operationBannerIcons';

interface DbUpgradeTaskBannerProps {
  task: Task;
  universeUuid: string;
}

export const DbUpgradeTaskBanner = ({ task, universeUuid }: DbUpgradeTaskBannerProps) => {
  const [isDbUpgradeManagementSidePanelOpen, setIsDbUpgradeManagementSidePanelOpen] =
    useState(false);
  const [isDbUpgradeRollBackModalOpen, setIsDbUpgradeRollBackModalOpen] = useState(false);
  const [isDbUpgradeFinalizeModalOpen, setIsDbUpgradeFinalizeModalOpen] = useState(false);
  const { t } = useTranslation('translation', {
    keyPrefix: 'universeActions.dbUpgrade.clusterBanner'
  });
  const isDbUpgradeTask = getIsDbUpgradeTask(task);
  const targetDbVersion = task.details?.versionNumbers?.ybSoftwareVersion ?? '';

  const universeDetailsQuery = useQuery(
    universeQueryKey.detailsV2(universeUuid),
    () => getUniverse(universeUuid),
    {
      refetchInterval: PollingIntervalMs.FOCUSED_TASK
    }
  );

  const dbUpgradeMetadataQuery = useQuery(
    dbUpgradeMetadataQueryKey.detail(universeUuid, {
      yb_software_version: targetDbVersion ?? ''
    }),
    () =>
      precheckSoftwareUpgrade(universeUuid, {
        yb_software_version: targetDbVersion ?? ''
      }),
    {
      enabled: !!targetDbVersion && isDbUpgradeTask
    }
  );

  if (!isDbUpgradeTask || !universeDetailsQuery.data?.info?.software_upgrade_state) {
    return null;
  }

  const { ysql_major_version_upgrade: isYsqlMajorUpgrade = false } =
    dbUpgradeMetadataQuery.data ?? {};
  let bannerComponent = null;
  const getOpenDbUpgradeManagementSidePanelButton = (buttonLabel: string) => (
    <YBButton
      variant="secondary"
      size="medium"
      data-testid="open-upgrade-monitor-button"
      onClick={() => setIsDbUpgradeManagementSidePanelOpen(true)}
    >
      {buttonLabel}
    </YBButton>
  );
  const openUpgradeMonitorButton = getOpenDbUpgradeManagementSidePanelButton(
    t('actions.openUpgradeMonitor')
  );
  const openUpgradeMonitorToContinueButton = getOpenDbUpgradeManagementSidePanelButton(
    t('actions.openUpgradeMonitorToContinue')
  );
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
          title={t('upgradingSoftware.title')}
          content={
            <OperationBannerProgressContent progressPercent={task.percentComplete ?? 0} />
          }
          action={openUpgradeMonitorButton}
          message={
            <Trans
              t={t}
              i18nKey={
                isYsqlMajorUpgrade
                  ? 'upgradingSoftware.descriptionMajorDbUpgrade'
                  : 'upgradingSoftware.description'
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
    case TaskState.PAUSED:
      bannerComponent = (
        <YBOperationBanner
          variant={OperationBannerVariant.Warning}
          dense
          minHeight={46}
          showDivider={false}
          iconCircle={false}
          icon={<OperationBannerWaveIcon />}
          title={t('upgradePausedForMonitoring.title')}
          content={
            <OperationBannerProgressContent progressPercent={task.percentComplete ?? 0} />
          }
          action={openUpgradeMonitorToContinueButton}
          message={t('upgradePausedForMonitoring.description')}
        />
      );
      break;
    case TaskState.SUCCESS:
      if (
        universeDetailsQuery.data?.info?.software_upgrade_state ===
        UniverseInfoSoftwareUpgradeState.PreFinalize
      ) {
        bannerComponent = (
          <YBOperationBanner
            variant={OperationBannerVariant.Warning}
            dense
            minHeight={46}
            showDivider={false}
            iconCircle={false}
            icon={<OperationBannerWaveIcon />}
            title={t('finalizeOrRollBack.title')}
            action={openUpgradeMonitorToContinueButton}
            message={t('finalizeOrRollBack.description')}
          />
        );
      } else if (
        universeDetailsQuery.data?.info?.software_upgrade_state ===
        UniverseInfoSoftwareUpgradeState.Ready
      ) {
        bannerComponent = (
          <YBOperationBanner
            variant={OperationBannerVariant.Success}
            dense
            minHeight={46}
            showDivider={false}
            title={t('upgradeCompleted.title', {
              targetDbVersion: formatYbSoftwareVersionString(targetDbVersion ?? '')
            })}
            message={
              <Trans
                t={t}
                i18nKey="upgradeCompleted.description"
                components={{
                  rollBackLinkButton: (
                    <MUILink
                      onClick={() => setIsDbUpgradeRollBackModalOpen(true)}
                      underline="always"
                    />
                  )
                }}
              />
            }
          />
        );
      }
      break;
    case TaskState.FAILURE:
    case TaskState.ABORTED:
      if (
        universeDetailsQuery.data?.info?.software_upgrade_state ===
        UniverseInfoSoftwareUpgradeState.Ready
      ) {
        bannerComponent = (
          <YBOperationBanner
            variant={OperationBannerVariant.Warning}
            dense
            minHeight={46}
            showDivider={false}
            title={t('upgradeAborted.title')}
            message={t('upgradeAborted.description')}
            action={openUpgradeMonitorButton}
          />
        );
      } else {
        bannerComponent = (
          <YBOperationBanner
            variant={OperationBannerVariant.Error}
            dense
            minHeight={46}
            showDivider={false}
            title={t('softwareUpgradeFailed.title')}
            content={
              <OperationBannerProgressContent
                progressPercent={task.percentComplete ?? 0}
                state={YBProgressBarState.Error}
              />
            }
            action={openUpgradeMonitorButton}
          />
        );
      }
      break;
    case TaskState.CREATED:
    case TaskState.INITIALIZING:
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
      {isDbUpgradeManagementSidePanelOpen && (
        <DbUpgradeManagementSidePanel
          modalProps={{
            open: isDbUpgradeManagementSidePanelOpen,
            onClose: () => setIsDbUpgradeManagementSidePanelOpen(false)
          }}
          universeUuid={universeUuid}
        />
      )}
      {isDbUpgradeRollBackModalOpen && (
        <DbUpgradeRollBackModal
          modalProps={{
            open: isDbUpgradeRollBackModalOpen,
            onClose: () => setIsDbUpgradeRollBackModalOpen(false)
          }}
          universeUuid={universeUuid}
        />
      )}
      {isDbUpgradeFinalizeModalOpen && (
        <DbUpgradeFinalizeModal
          modalProps={{
            open: isDbUpgradeFinalizeModalOpen,
            onClose: () => setIsDbUpgradeFinalizeModalOpen(false)
          }}
          universeUuid={universeUuid}
        />
      )}
    </>
  );
};
