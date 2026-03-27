import {
  AZUpgradeState,
  AZUpgradeStatus,
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

export interface DbUpgradeStages {
  preCheckStage: AccordionCardState;
  upgradeMasterServersStage: AccordionCardState;
  upgradeAzStages: Record<string, AccordionCardState>;
  finalizeStage: AccordionCardState;
}
export const classifyDbUpgradeStages = (dbUpgradeTask: Task): DbUpgradeStages => {
  if (!dbUpgradeTask.canaryUpgradeProgress) {
    return {
      preCheckStage: AccordionCardState.NEUTRAL,
      upgradeMasterServersStage: AccordionCardState.NEUTRAL,
      upgradeAzStages: {},
      finalizeStage: AccordionCardState.NEUTRAL
    };
  }

  const preCheckStage = mapDbUpgradePrecheckStatusToAccordionCardState(
    dbUpgradeTask.canaryUpgradeProgress.precheckStatus
  );
  const upgradeMasterServersStage = classifyUpgradeMasterServersStage(
    dbUpgradeTask.canaryUpgradeProgress.masterAZUpgradeStatesList
  );
  const upgradeAzStages =
    dbUpgradeTask.canaryUpgradeProgress.tserverAZUpgradeStatesList?.reduce(
      (accumulator, azUpgradeState) => ({
        ...accumulator,
        [azUpgradeState.azUUID]: mapAzUpgradeStatusToAccordionCardState(azUpgradeState.status)
      }),
      {} as Record<string, AccordionCardState>
    ) ?? {};
  const finalizeStage = AccordionCardState.NEUTRAL;
  return { preCheckStage, upgradeMasterServersStage, upgradeAzStages, finalizeStage };
};
