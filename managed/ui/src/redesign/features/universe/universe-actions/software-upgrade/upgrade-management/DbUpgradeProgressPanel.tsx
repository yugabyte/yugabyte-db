import { useState } from 'react';
import { yba, YBTag } from '@yugabyte-ui-library/core';
import { makeStyles, Typography } from '@material-ui/core';
import clsx from 'clsx';
import { Trans, useTranslation } from 'react-i18next';
import { AxiosError } from 'axios';
import { useQueryClient } from 'react-query';

import { CanaryPauseState, Task } from '@app/redesign/features/tasks/dtos';
import { getPrimaryCluster, getReadOnlyCluster } from '@app/redesign/utils/universeUtils';
import { formatYbSoftwareVersionString } from '@app/utils/Formatters';
import { Universe } from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';
import { getPlacementAzMetadataList } from '../utils/formUtils';
import { AccordionCard, AccordionCardState } from './AccordionCard';
import { AssessPerformancePrompt } from './AssessPerformancePrompt';
import { UpgradeStageCategory } from './constants';
import { PreCheckStageBanner } from './PreCheckStageBanner';
import { UpgradeStageBanner } from './UpgradeStageBanner';
import { classifyDbUpgradeStages, getTserverAzClusterUpgradeStageKey } from './utils';
import { TemporaryRestrictionsNotice } from './TemporaryRestrictionsNotice';
import { DbUpgradeRollBackModal } from '../DbUpgradeRollBackModal';
import { DbUpgradeFinalizeModal } from '../DbUpgradeFinalizeModal';
import { useResumeCanarySoftwareUpgrade } from '@app/v2/api/universe/universe';
import { handleServerError } from '@app/utils/errorHandlingUtils';
import { ApiPermissionMap } from '@app/redesign/features/rbac/ApiAndUserPermMapping';
import { RbacValidator } from '@app/redesign/features/rbac/common/RbacApiPermValidator';
import { useRefreshSoftwareUpgradeTasksCache } from '@app/redesign/helpers/cacheUtils';

import ConnectIcon from '@app/redesign/assets/approved/connect.svg';

const YBButton = yba.YBButton;
interface DbUpgradeProgressPanelProps {
  dbUpgradeTask: Task;
  universe: Universe | undefined;
  isYsqlMajorUpgrade: boolean;
  className?: string;
}

const useStyles = makeStyles((theme) => ({
  progressPanel: {
    display: 'flex',
    flexDirection: 'column',
    gap: theme.spacing(2),

    padding: theme.spacing(2),

    backgroundColor: theme.palette.grey[50],
    border: `1px solid ${theme.palette.grey[200]}`,
    borderRadius: theme.shape.borderRadius,

    '& .MuiTypography-body2': {
      lineHeight: '20px'
    },
    '& .MuiTypography-subtitle1': {
      lineHeight: '18px'
    }
  },
  title: {
    color: theme.palette.grey[800],
    fontSize: 15,
    fontWeight: 600,
    fontStyle: 'normal',
    lineHeight: '20px'
  },
  infoText: {
    color: theme.palette.grey[700]
  },
  cardButtonsContainer: {
    display: 'flex',
    justifyContent: 'flex-end',
    gap: theme.spacing(1)
  },
  headerAccessoriesSection: {
    marginLeft: theme.spacing(1)
  }
}));

const TSERVER_AZ_UPGRADE_STAGE_START_INDEX = 3;
export const DbUpgradeProgressPanel = ({
  dbUpgradeTask,
  universe,
  className,
  isYsqlMajorUpgrade
}: DbUpgradeProgressPanelProps) => {
  const [isDbUpgradeRollbackModalOpen, setIsDbUpgradeRollbackModalOpen] = useState(false);
  const [isDbUpgradeFinalizeModalOpen, setIsDbUpgradeFinalizeModalOpen] = useState(false);
  const classes = useStyles();
  const universeUuid = universe?.info?.universe_uuid ?? '';
  const refreshSoftwareUpgradeTasksCache = useRefreshSoftwareUpgradeTasksCache(universeUuid);
  const { t } = useTranslation('translation', {
    keyPrefix: 'universeActions.dbUpgrade.dbUpgradeManagementSidePanel.progressPanel'
  });

  const resumeUpgradeMutation = useResumeCanarySoftwareUpgrade({
    mutation: {
      onSuccess: () => {
        refreshSoftwareUpgradeTasksCache();
      },
      onError: (error: Error | AxiosError) =>
        handleServerError(error, {
          customErrorLabel: t('toast.resumeUpgradeFailedLabel')
        })
    }
  });

  const resumeCanaryRbacAccessRequiredOn = {
    onResource: universeUuid,
    ...ApiPermissionMap.UPGRADE_UNIVERSE_RESUME_CANARY
  };

  const rollbackRbacAccessRequiredOn = {
    onResource: universeUuid,
    ...ApiPermissionMap.UPGRADE_UNIVERSE_ROLLBACK
  };

  const finalizeRbacAccessRequiredOn = {
    onResource: universeUuid,
    ...ApiPermissionMap.UPGRADE_UNIVERSE_FINALIZE
  };

  const handleRollbackUpgrade = () => {
    setIsDbUpgradeRollbackModalOpen(true);
  };
  const handleResumeUpgrade = () => {
    if (!universeUuid) {
      return;
    }
    resumeUpgradeMutation.mutate({
      uniUUID: universeUuid,
      data: {
        task_uuid: dbUpgradeTask.id ?? ''
      }
    });
  };

  const handleFinalizeUpgrade = () => {
    setIsDbUpgradeFinalizeModalOpen(true);
  };

  const targetDbVersion = dbUpgradeTask?.details?.versionNumbers?.ybSoftwareVersion;
  const dbUpgradeTaskPauseState = dbUpgradeTask?.softwareUpgradeProgress?.canaryPauseState;
  const tserverAZUpgradeStatesList =
    dbUpgradeTask?.softwareUpgradeProgress?.tserverAZUpgradeStatesList;

  const clusters = universe?.spec?.clusters ?? [];
  const upgradedAzMetadataList = getPlacementAzMetadataList(getPrimaryCluster(clusters)) ?? [];
  const upgradedAzDisplayNameByUuid = Object.fromEntries(
    upgradedAzMetadataList.map((az) => [az.azUuid, az.displayName])
  );
  const readReplicaClusterUuid = getReadOnlyCluster(clusters)?.uuid;
  const upgradeAzStageCount = tserverAZUpgradeStatesList?.length ?? 0;
  const { preCheckStage, upgradeMasterServersStage, upgradeAzStages, finalizeStage } =
    classifyDbUpgradeStages(dbUpgradeTask);
  const isPausedAfterSuccessfulMasterServersUpgrade =
    dbUpgradeTaskPauseState === CanaryPauseState.PAUSED_AFTER_MASTERS &&
    upgradeMasterServersStage === AccordionCardState.SUCCESS;
  const isTserverAzUpgradeStagesCompleted = Object.values(upgradeAzStages).every(
    (azUpgradeStage) => azUpgradeStage.accordionCardState === AccordionCardState.SUCCESS
  );
  const targetDbVersionLabel = targetDbVersion
    ? formatYbSoftwareVersionString(targetDbVersion)
    : '-';
  const taskUuid = dbUpgradeTask.id ?? '';

  return (
    <div className={clsx(classes.progressPanel, className)}>
      <Typography variant="h5" className={classes.title}>
        {t('title')}
      </Typography>
      <AccordionCard title={t('preCheckStage.title')} stepNumber={1} state={preCheckStage}>
        <Typography variant="subtitle1" className={classes.infoText}>
          {t('preCheckStage.description')}
        </Typography>
        <PreCheckStageBanner
          state={preCheckStage}
          universeUuid={universeUuid}
          taskUuid={taskUuid}
        />
      </AccordionCard>
      <AccordionCard
        title={t('upgradeMasterServersStage.title')}
        stepNumber={2}
        state={upgradeMasterServersStage}
      >
        <Typography variant="subtitle1" className={classes.infoText}>
          <Trans
            t={t}
            i18nKey="upgradeMasterServersStage.description"
            components={{ bold: <b /> }}
            values={{ version: targetDbVersionLabel }}
          />
        </Typography>
        {upgradeMasterServersStage === AccordionCardState.NEUTRAL && (
          <TemporaryRestrictionsNotice
            upgradeStageCategory={UpgradeStageCategory.UPGRADE}
            isYsqlMajorUpgrade={isYsqlMajorUpgrade}
          />
        )}
        {isPausedAfterSuccessfulMasterServersUpgrade && (
          <AssessPerformancePrompt upgradeStageCategory={UpgradeStageCategory.UPGRADE} />
        )}
        <UpgradeStageBanner
          state={upgradeMasterServersStage}
          universeUuid={universeUuid}
          taskUuid={taskUuid}
        />
        {isPausedAfterSuccessfulMasterServersUpgrade && (
          <div className={classes.cardButtonsContainer}>
            <RbacValidator accessRequiredOn={rollbackRbacAccessRequiredOn} isControl>
              <YBButton
                variant="secondary"
                onClick={handleRollbackUpgrade}
                dataTestId="roll-back-upgrade-button-masters"
              >
                {t('rollBack')}
              </YBButton>
            </RbacValidator>
            <RbacValidator accessRequiredOn={resumeCanaryRbacAccessRequiredOn} isControl>
              <YBButton
                variant="ybaPrimary"
                onClick={handleResumeUpgrade}
                disabled={resumeUpgradeMutation.isLoading}
                dataTestId="resume-upgrade-button-masters"
              >
                {t('resumeUpgrade')}
              </YBButton>
            </RbacValidator>
          </div>
        )}
      </AccordionCard>
      {(tserverAZUpgradeStatesList ?? []).map((azUpgradeState, index) => {
        const tserverAzClusterStageKey = getTserverAzClusterUpgradeStageKey(
          azUpgradeState.azUUID,
          azUpgradeState.clusterUUID
        );
        const azUpgradeStagePresentation = upgradeAzStages[tserverAzClusterStageKey];
        const cardState =
          azUpgradeStagePresentation?.accordionCardState ?? AccordionCardState.NEUTRAL;
        const isPausedAfterSuccessfulUpgrade =
          cardState === AccordionCardState.SUCCESS &&
          dbUpgradeTaskPauseState === CanaryPauseState.PAUSED_AFTER_TSERVERS_AZ &&
          azUpgradeStagePresentation?.isLastAzBeforeCanaryPause;
        return (
          <AccordionCard
            key={tserverAzClusterStageKey}
            title={t('upgradeAzStage.title', {
              azLabel: upgradedAzDisplayNameByUuid[azUpgradeState.azUUID] ?? azUpgradeState.azName
            })}
            stepNumber={TSERVER_AZ_UPGRADE_STAGE_START_INDEX + index}
            state={cardState}
            headerAccessories={
              readReplicaClusterUuid &&
              azUpgradeState.clusterUUID === readReplicaClusterUuid ? (
                <div className={classes.headerAccessoriesSection}>
                  <YBTag size="medium" variant="dark">
                    {t('readReplicaTag')}
                  </YBTag>
                </div>
              ) : undefined
            }
          >
            <Typography variant="subtitle1" className={classes.infoText}>
              <Trans
                t={t}
                i18nKey="upgradeAzStage.description"
                components={{ bold: <b /> }}
                values={{ azName: azUpgradeState.azName, version: targetDbVersionLabel }}
              />
            </Typography>
            {cardState === AccordionCardState.NEUTRAL && (
              <TemporaryRestrictionsNotice
                upgradeStageCategory={UpgradeStageCategory.UPGRADE}
                isYsqlMajorUpgrade={isYsqlMajorUpgrade}
              />
            )}
            {isPausedAfterSuccessfulUpgrade && (
              <AssessPerformancePrompt upgradeStageCategory={UpgradeStageCategory.UPGRADE} />
            )}
            <UpgradeStageBanner state={cardState} universeUuid={universeUuid} taskUuid={taskUuid} />
            {isPausedAfterSuccessfulUpgrade && (
              <div className={classes.cardButtonsContainer}>
                <RbacValidator accessRequiredOn={rollbackRbacAccessRequiredOn} isControl>
                  <YBButton
                    variant="secondary"
                    onClick={handleRollbackUpgrade}
                    dataTestId={`roll-back-upgrade-button-tserverAz-${tserverAzClusterStageKey}`}
                  >
                    {t('rollBack')}
                  </YBButton>
                </RbacValidator>
                <RbacValidator accessRequiredOn={resumeCanaryRbacAccessRequiredOn} isControl>
                  <YBButton
                    variant="ybaPrimary"
                    onClick={handleResumeUpgrade}
                    disabled={resumeUpgradeMutation.isLoading}
                    dataTestId={`resume-upgrade-button-tserverAz-${tserverAzClusterStageKey}`}
                  >
                    {t('resumeUpgrade')}
                  </YBButton>
                </RbacValidator>
              </div>
            )}
          </AccordionCard>
        );
      })}
      <AccordionCard
        title={t('finalizeStage.title')}
        stepNumber={TSERVER_AZ_UPGRADE_STAGE_START_INDEX + upgradeAzStageCount}
        state={finalizeStage}
      >
        <AssessPerformancePrompt upgradeStageCategory={UpgradeStageCategory.FINALIZE} />
        {finalizeStage === AccordionCardState.NEUTRAL && (
          <TemporaryRestrictionsNotice
            upgradeStageCategory={UpgradeStageCategory.FINALIZE}
            isYsqlMajorUpgrade={isYsqlMajorUpgrade}
          />
        )}
        {isTserverAzUpgradeStagesCompleted && (
          <div className={classes.cardButtonsContainer}>
            <RbacValidator accessRequiredOn={rollbackRbacAccessRequiredOn} isControl>
              <YBButton
                variant="secondary"
                onClick={handleRollbackUpgrade}
                dataTestId="roll-back-upgrade-button-finalize"
              >
                {t('rollBack')}
              </YBButton>
            </RbacValidator>
            <RbacValidator accessRequiredOn={finalizeRbacAccessRequiredOn} isControl>
              <YBButton
                variant="ybaPrimary"
                startIcon={<ConnectIcon width={24} height={24} />}
                onClick={handleFinalizeUpgrade}
                dataTestId="finalize-upgrade-now-button"
              >
                {t('finalizeUpgradeNow')}
              </YBButton>
            </RbacValidator>
          </div>
        )}
      </AccordionCard>
      <DbUpgradeRollBackModal
        modalProps={{
          open: isDbUpgradeRollbackModalOpen,
          onClose: () => setIsDbUpgradeRollbackModalOpen(false)
        }}
        universeUuid={universeUuid}
      />
      <DbUpgradeFinalizeModal
        modalProps={{
          open: isDbUpgradeFinalizeModalOpen,
          onClose: () => setIsDbUpgradeFinalizeModalOpen(false)
        }}
        universeUuid={universeUuid}
      />
    </div>
  );
};
