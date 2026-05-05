import {
  AZUpgradeState,
  AZUpgradeStatus,
  CanaryPauseState,
  DbUpgradePrecheckStatus,
  Task
} from '@app/redesign/features/tasks/dtos';
import { AccordionCardState } from './AccordionCard';

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
  upgradeAzStages: Record<string, AzUpgradeStageMetadata>;
  finalizeStage: AccordionCardState;
}

export type DbUpgradeStages = DbUpgradeStagesMetadata;

/**
 * Classifies a DB software-upgrade task into accordion card states for the progress panel.
 
 * Assumptions:
 * - tserverAzUpgradeStatesList returned by the backend is ordered by AZ upgrade order.
 */
export const classifyDbUpgradeStages = (dbUpgradeTask: Task): DbUpgradeStagesMetadata => {
  if (!dbUpgradeTask.softwareUpgradeProgress) {
    return {
      preCheckStage: AccordionCardState.NEUTRAL,
      upgradeMasterServersStage: AccordionCardState.NEUTRAL,
      upgradeAzStages: {},
      finalizeStage: AccordionCardState.NEUTRAL
    };
  }

  const softwareUpgradeProgress = dbUpgradeTask.softwareUpgradeProgress;
  const preCheckStage = mapDbUpgradePrecheckStatusToAccordionCardState(
    softwareUpgradeProgress.precheckStatus
  );
  const upgradeMasterServersStage = classifyUpgradeMasterServersStage(
    softwareUpgradeProgress.masterAZUpgradeStatesList
  );

  const tserverAZUpgradeStatesList = softwareUpgradeProgress.tserverAZUpgradeStatesList ?? [];
  const upgradeAzStages: Record<string, AzUpgradeStageMetadata> = {};
  let lastCompletedTserverAzUUID: string | undefined;
  let hasNotStartedTserverAz = false;

  for (const azUpgradeState of tserverAZUpgradeStatesList) {
    if (azUpgradeState.status === AZUpgradeStatus.NOT_STARTED) {
      hasNotStartedTserverAz = true;
    }
    if (azUpgradeState.status === AZUpgradeStatus.COMPLETED) {
      lastCompletedTserverAzUUID = azUpgradeState.azUUID;
    }
    upgradeAzStages[azUpgradeState.azUUID] = {
      accordionCardState: mapAzUpgradeStatusToAccordionCardState(azUpgradeState.status),
      isLastAzBeforeCanaryPause: false
    };
  }

  if (
    softwareUpgradeProgress.canaryPauseState === CanaryPauseState.PAUSED_AFTER_TSERVERS_AZ &&
    hasNotStartedTserverAz &&
    lastCompletedTserverAzUUID !== undefined
  ) {
    const azUpgradeStageMetadata = upgradeAzStages[lastCompletedTserverAzUUID];
    if (azUpgradeStageMetadata) {
      azUpgradeStageMetadata.isLastAzBeforeCanaryPause = true;
    }
  }

  const finalizeStage = AccordionCardState.NEUTRAL;
  return { preCheckStage, upgradeMasterServersStage, upgradeAzStages, finalizeStage };
};
