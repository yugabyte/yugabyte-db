import {
  AZUpgradeState,
  AZUpgradeStatus,
  CanaryPauseState,
  DbUpgradePrecheckStatus,
  type SoftwareUpgradeProgress,
  Task
} from '@app/redesign/features/tasks/dtos';
import { AccordionCardState } from './AccordionCard';

export const getTaskSoftwareUpgradeProgress = (
  task: Task | null | undefined
): SoftwareUpgradeProgress | null | undefined => task?.details?.softwareUpgradeProgress;

/** Returns a unique key for a t-server AZ upgrade stage.
 * Just using azUuid is not enough because the same AZ UUID can appear on multiple clusters.
 */
export const getTserverAzClusterUpgradeStageKey = (azUUID: string, clusterUUID: string): string =>
  `${azUUID}|${clusterUUID}`;

const mapAzUpgradeStatusToAccordionCardState = (status: AZUpgradeStatus): AccordionCardState => {
  switch (status) {
    case AZUpgradeStatus.NOT_STARTED:
      return AccordionCardState.NEUTRAL;
    case AZUpgradeStatus.IN_PROGRESS:
      return AccordionCardState.IN_PROGRESS;
    case AZUpgradeStatus.COMPLETED:
      return AccordionCardState.SUCCESS;
    case AZUpgradeStatus.FAILED:
      return AccordionCardState.FAILED;
    default:
      return AccordionCardState.NEUTRAL;
  }
};

const mapDbUpgradePrecheckStatusToAccordionCardState = (
  precheckStatus: DbUpgradePrecheckStatus
): AccordionCardState => {
  switch (precheckStatus) {
    case DbUpgradePrecheckStatus.SUCCESS:
      return AccordionCardState.SUCCESS;
    case DbUpgradePrecheckStatus.RUNNING:
      return AccordionCardState.IN_PROGRESS;
    case DbUpgradePrecheckStatus.FAILED:
      return AccordionCardState.WARNING;
    default:
      return AccordionCardState.NEUTRAL;
  }
};

/**
 * Aggregates per-AZ master upgrade rows into one accordion state.
 *
 * Precedence:
 * - all COMPLETED → SUCCESS;
 * - any FAILED → FAILED;
 * - any IN_PROGRESS → IN_PROGRESS;
 * - mix of COMPLETED + NOT_STARTED → IN_PROGRESS;
 * - no AZs or all AZ upgrades are in NOT_STARTED state → NEUTRAL.
 */
const classifyUpgradeMasterServersStage = (
  masterAZUpgradeStatesList: AZUpgradeState[] | undefined
): AccordionCardState => {
  const azUpgradeStates = masterAZUpgradeStatesList ?? [];
  if (!azUpgradeStates.length) {
    return AccordionCardState.NEUTRAL;
  }

  let hasInProgressAz = false;
  let completedAzCount = 0;
  let notStartedAzCount = 0;

  for (const azUpgradeState of azUpgradeStates) {
    switch (azUpgradeState.status) {
      case AZUpgradeStatus.FAILED:
        return AccordionCardState.FAILED;
      case AZUpgradeStatus.IN_PROGRESS:
        hasInProgressAz = true;
        break;
      case AZUpgradeStatus.COMPLETED:
        completedAzCount += 1;
        break;
      case AZUpgradeStatus.NOT_STARTED:
        notStartedAzCount += 1;
        break;
      default:
        break;
    }
  }

  if (hasInProgressAz) {
    return AccordionCardState.IN_PROGRESS;
  }
  if (completedAzCount === azUpgradeStates.length) {
    return AccordionCardState.SUCCESS;
  }
  if (completedAzCount > 0 && notStartedAzCount > 0) {
    return AccordionCardState.IN_PROGRESS;
  }
  return AccordionCardState.NEUTRAL;
};

export interface AzUpgradeStageMetadata {
  accordionCardState: AccordionCardState;
  isLastAzBeforeCanaryPause: boolean;
}

export interface DbUpgradeStagesMetadata {
  preCheckStage: AccordionCardState;
  upgradeMasterServersStage: AccordionCardState;
  /** Keys from {@link getTserverAzClusterUpgradeStageKey}(azUUID, clusterUUID). */
  upgradeAzStages: Record<string, AzUpgradeStageMetadata>;
  finalizeStage: AccordionCardState;
}

export type DbUpgradeStages = DbUpgradeStagesMetadata;

/**
 * Classifies a DB software-upgrade task into accordion card states for the progress panel.
 
 * Assumptions:
 * - tserverAZUpgradeStatesList returned by the backend is ordered by upgrade order (may repeat the same AZ UUID across clusters).
 */
export const classifyDbUpgradeStages = (dbUpgradeTask: Task): DbUpgradeStagesMetadata => {
  const softwareUpgradeProgress = getTaskSoftwareUpgradeProgress(dbUpgradeTask);
  if (!softwareUpgradeProgress) {
    return {
      preCheckStage: AccordionCardState.NEUTRAL,
      upgradeMasterServersStage: AccordionCardState.NEUTRAL,
      upgradeAzStages: {},
      finalizeStage: AccordionCardState.NEUTRAL
    };
  }

  const preCheckStage = mapDbUpgradePrecheckStatusToAccordionCardState(
    softwareUpgradeProgress.precheckStatus
  );
  const upgradeMasterServersStage = classifyUpgradeMasterServersStage(
    softwareUpgradeProgress.masterAZUpgradeStatesList
  );

  const tserverAZUpgradeStatesList = softwareUpgradeProgress.tserverAZUpgradeStatesList ?? [];
  const upgradeAzStages: Record<string, AzUpgradeStageMetadata> = {};
  let lastCompletedTserverStageKey: string | undefined;
  let hasNotStartedTserverAz = false;

  for (const azUpgradeState of tserverAZUpgradeStatesList) {
    if (azUpgradeState.status === AZUpgradeStatus.NOT_STARTED) {
      hasNotStartedTserverAz = true;
    }
    const stageKey = getTserverAzClusterUpgradeStageKey(
      azUpgradeState.azUUID,
      azUpgradeState.clusterUUID
    );
    if (azUpgradeState.status === AZUpgradeStatus.COMPLETED) {
      lastCompletedTserverStageKey = stageKey;
    }
    upgradeAzStages[stageKey] = {
      accordionCardState: mapAzUpgradeStatusToAccordionCardState(azUpgradeState.status),
      isLastAzBeforeCanaryPause: false
    };
  }

  if (
    softwareUpgradeProgress.canaryPauseState === CanaryPauseState.PAUSED_AFTER_TSERVERS_AZ &&
    hasNotStartedTserverAz &&
    lastCompletedTserverStageKey !== undefined
  ) {
    const azUpgradeStageMetadata = upgradeAzStages[lastCompletedTserverStageKey];
    if (azUpgradeStageMetadata) {
      azUpgradeStageMetadata.isLastAzBeforeCanaryPause = true;
    }
  }

  const finalizeStage = AccordionCardState.NEUTRAL;
  return { preCheckStage, upgradeMasterServersStage, upgradeAzStages, finalizeStage };
};
