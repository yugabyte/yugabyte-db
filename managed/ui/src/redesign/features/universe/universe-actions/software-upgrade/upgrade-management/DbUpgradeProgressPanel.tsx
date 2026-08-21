import { useEffect, useRef, useState } from 'react';
import { yba, YBTag } from '@yugabyte-ui-library/core';
import { makeStyles, Typography } from '@material-ui/core';
import clsx from 'clsx';
import { Trans, useTranslation } from 'react-i18next';
import { AxiosError } from 'axios';

import { CanaryPauseState, Task } from '@app/redesign/features/tasks/dtos';
import { getReadOnlyCluster } from '@app/redesign/utils/universeUtils';
import { formatYbSoftwareVersionString } from '@app/utils/Formatters';
import {
  Universe,
  UniverseInfoSoftwareUpgradeState
} from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';
import { getPlacementAzDisplayNameForCluster } from '../utils/formUtils';
import { AccordionCard, AccordionCardState } from './AccordionCard';
import { AssessPerformancePrompt } from './AssessPerformancePrompt';
import { UpgradeStageCategory } from './constants';
import { PreCheckStageBanner } from './PreCheckStageBanner';
import { UpgradeStageBanner } from './UpgradeStageBanner';
import {
  ActiveAccordionId,
  classifyDbUpgradeStages,
  getActiveDbUpgradeProgressAccordionId,
  getTaskSoftwareUpgradeProgress,
  getTserverAzAccordionId,
  getTserverAzClusterUpgradeStageKey
} from './utils';
import { TemporaryRestrictionsNotice } from './TemporaryRestrictionsNotice';
import { DbUpgradeRollBackModal } from '../DbUpgradeRollBackModal';
import { DbUpgradeFinalizeModal } from '../DbUpgradeFinalizeModal';
import { useResumeCanarySoftwareUpgrade } from '@app/v2/api/universe/universe';
import { handleServerError } from '@app/utils/errorHandlingUtils';
import { ApiPermissionMap } from '@app/redesign/features/rbac/ApiAndUserPermMapping';
import { RbacValidator } from '@app/redesign/features/rbac/common/RbacApiPermValidator';
import { useRefreshSoftwareUpgradeTasksCache } from '@app/redesign/helpers/cacheUtils';
import { isNonEmptyObject } from '@app/utils/ObjectUtils';

import ConnectIcon from '@app/redesign/assets/approved/connect.svg';

const YBButton = yba.YBButton;
interface DbUpgradeProgressPanelProps {
  dbUpgradeTask: Task;
  universe: Universe | undefined;
  isDbUpgradeFinalizeRequired: boolean;
  isYsqlMajorUpgrade: boolean;
  onCloseSidePanel: () => void;
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
  isDbUpgradeFinalizeRequired,
  className,
  isYsqlMajorUpgrade,
  onCloseSidePanel
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
  const softwareUpgradeProgress = getTaskSoftwareUpgradeProgress(dbUpgradeTask);
  const dbUpgradeTaskPauseState = softwareUpgradeProgress?.canaryPauseState;
  const tserverAZUpgradeStatesList = softwareUpgradeProgress?.tserverAZUpgradeStatesList;
  const clusters = universe?.spec?.clusters ?? [];
  const readReplicaClusterUuid = getReadOnlyCluster(clusters)?.uuid;
  const upgradeAzStageCount = tserverAZUpgradeStatesList?.length ?? 0;
  const softwareUpgradeState = universe?.info?.software_upgrade_state;
  const stages = classifyDbUpgradeStages(dbUpgradeTask, softwareUpgradeState);
  const { preCheckStage, upgradeMasterServersStage, upgradeAzStages, finalizeStage } = stages;
  const isPausedAfterSuccessfulMasterServersUpgrade =
    dbUpgradeTaskPauseState === CanaryPauseState.PAUSED_AFTER_MASTERS &&
    upgradeMasterServersStage === AccordionCardState.SUCCESS;
  const targetDbVersionLabel = targetDbVersion
    ? formatYbSoftwareVersionString(targetDbVersion)
    : '-';
  const taskUuid = dbUpgradeTask.id ?? '';

  // Only one accordion is expanded at a time. Seed the initial expansion to the
  // currently active stage; after first render, expansion is purely user-driven.
  const [currentExpandedStep, setCurrentExpandedStep] = useState<string | null>(() =>
    getActiveDbUpgradeProgressAccordionId({
      stages,
      dbUpgradeTaskPauseState,
      tserverAZUpgradeStatesList,
      softwareUpgradeState
    })
  );
  const handleToggleStage = (stageId: string, isExpanded: boolean) => {
    setCurrentExpandedStep(isExpanded ? stageId : null);
  };
  const accordionElementRefs = useRef<Map<string, HTMLElement>>(new Map());
  const setAccordionElementRef = (accordionId: string) => (element: HTMLElement | null) => {
    if (element === null) {
      accordionElementRefs.current.delete(accordionId);
    } else {
      accordionElementRefs.current.set(accordionId, element);
    }
  };

  // Scroll the initially expanded card into view when the side panel mounts.
  useEffect(() => {
    if (currentExpandedStep === null) {
      return;
    }
    accordionElementRefs.current
      .get(currentExpandedStep)
      ?.scrollIntoView({ block: 'start', behavior: 'auto' });
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  const getAccordionPropsForId = (accordionId: string) => ({
    expanded: currentExpandedStep === accordionId,
    onChange: (_event: React.ChangeEvent<{}>, isExpanded: boolean) =>
      handleToggleStage(accordionId, isExpanded)
  });
  return (
    <div className={clsx(classes.progressPanel, className)}>
      <Typography variant="h5" className={classes.title}>
        {t('title')}
      </Typography>
      <AccordionCard
        title={t('preCheckStage.title')}
        stepNumber={1}
        state={preCheckStage}
        accordionProps={getAccordionPropsForId(ActiveAccordionId.PRE_CHECK)}
        ref={setAccordionElementRef(ActiveAccordionId.PRE_CHECK)}
      >
        <Typography variant="subtitle1" className={classes.infoText}>
          {t('preCheckStage.description')}
        </Typography>
        <PreCheckStageBanner
          state={preCheckStage}
          taskUuid={taskUuid}
          onCloseSidePanel={onCloseSidePanel}
        />
      </AccordionCard>
      {softwareUpgradeProgress && isNonEmptyObject(softwareUpgradeProgress) && (
        <>
          <AccordionCard
            title={t('upgradeMasterServersStage.title')}
            stepNumber={2}
            state={upgradeMasterServersStage}
            accordionProps={getAccordionPropsForId(ActiveAccordionId.UPGRADE_MASTER)}
            ref={setAccordionElementRef(ActiveAccordionId.UPGRADE_MASTER)}
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
              taskUuid={taskUuid}
              onCloseSidePanel={onCloseSidePanel}
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
            const tserverAzAccordionId = getTserverAzAccordionId(
              azUpgradeState.azUUID,
              azUpgradeState.clusterUUID
            );
            return (
              <AccordionCard
                key={tserverAzClusterStageKey}
                title={t('upgradeAzStage.title', {
                  azLabel: getPlacementAzDisplayNameForCluster(
                    clusters,
                    azUpgradeState.clusterUUID,
                    azUpgradeState.azUUID,
                    azUpgradeState.azName
                  )
                })}
                stepNumber={TSERVER_AZ_UPGRADE_STAGE_START_INDEX + index}
                state={cardState}
                accordionProps={getAccordionPropsForId(tserverAzAccordionId)}
                ref={setAccordionElementRef(tserverAzAccordionId)}
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
                <UpgradeStageBanner
                  state={cardState}
                  taskUuid={taskUuid}
                  onCloseSidePanel={onCloseSidePanel}
                />
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
          {isDbUpgradeFinalizeRequired && (
            <AccordionCard
              title={t('finalizeStage.title')}
              stepNumber={TSERVER_AZ_UPGRADE_STAGE_START_INDEX + upgradeAzStageCount}
              state={finalizeStage}
              accordionProps={getAccordionPropsForId(ActiveAccordionId.FINALIZE)}
              ref={setAccordionElementRef(ActiveAccordionId.FINALIZE)}
            >
              <AssessPerformancePrompt upgradeStageCategory={UpgradeStageCategory.FINALIZE} />
              {finalizeStage === AccordionCardState.NEUTRAL && (
                <TemporaryRestrictionsNotice
                  upgradeStageCategory={UpgradeStageCategory.FINALIZE}
                  isYsqlMajorUpgrade={isYsqlMajorUpgrade}
                />
              )}
              {universe?.info?.software_upgrade_state ===
                UniverseInfoSoftwareUpgradeState.PreFinalize && (
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
          )}
        </>
      )}
      <DbUpgradeRollBackModal
        modalProps={{
          open: isDbUpgradeRollbackModalOpen,
          onClose: () => {
            onCloseSidePanel();
            setIsDbUpgradeRollbackModalOpen(false);
          }
        }}
        universeUuid={universeUuid}
      />
      <DbUpgradeFinalizeModal
        modalProps={{
          open: isDbUpgradeFinalizeModalOpen,
          onClose: () => {
            onCloseSidePanel();
            setIsDbUpgradeFinalizeModalOpen(false);
          }
        }}
        universeUuid={universeUuid}
      />
    </div>
  );
};
