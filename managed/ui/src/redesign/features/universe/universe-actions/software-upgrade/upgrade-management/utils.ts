import {
  AZUpgradeState,
  AZUpgradeStatus,
  CanaryPauseState,
  DbUpgradePrecheckStatus,
  type SoftwareUpgradeProgress,
  Task,
  TaskState
} from '@app/redesign/features/tasks/dtos';
import { AccordionCardState } from './AccordionCard';
import { UniverseInfoSoftwareUpgradeState } from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';

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

const getPreCheckStage = (
  dbUpgradeTask: Task,
  softwareUpgradeState: UniverseInfoSoftwareUpgradeState | undefined
): DbUpgradePrecheckStatus => {
  if (
    dbUpgradeTask.status === TaskState.RUNNING &&
    softwareUpgradeState === UniverseInfoSoftwareUpgradeState.Ready
  ) {
    return DbUpgradePrecheckStatus.RUNNING;
  }
  if (
    dbUpgradeTask.status === TaskState.FAILURE &&
    softwareUpgradeState === UniverseInfoSoftwareUpgradeState.Ready
  ) {
    return DbUpgradePrecheckStatus.FAILED;
  }
  return DbUpgradePrecheckStatus.SUCCESS;
};
/**
 * Classifies a DB software-upgrade task into accordion card states for the progress panel.
 
 * Assumptions:
 * - tserverAZUpgradeStatesList returned by the backend is ordered by upgrade order (may repeat the same AZ UUID across clusters).
 */
export const classifyDbUpgradeStages = (
  dbUpgradeTask: Task,
  softwareUpgradeState: UniverseInfoSoftwareUpgradeState | undefined
): DbUpgradeStagesMetadata => {
  const softwareUpgradeProgress = getTaskSoftwareUpgradeProgress(dbUpgradeTask);
  if (!softwareUpgradeProgress) {
    return {
      preCheckStage: mapDbUpgradePrecheckStatusToAccordionCardState(
        getPreCheckStage(dbUpgradeTask, softwareUpgradeState)
      ),
      upgradeMasterServersStage: AccordionCardState.NEUTRAL,
      upgradeAzStages: {},
      finalizeStage: AccordionCardState.NEUTRAL
    };
  }

  const preCheckStage = mapDbUpgradePrecheckStatusToAccordionCardState(
    getPreCheckStage(dbUpgradeTask, softwareUpgradeState)
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

/**
 * Stable string ids for the non-AZ accordions in the DB upgrade progress panel.
 * Used to drive controlled expansion + auto-open behavior.
 */
export const ActiveAccordionId = {
  PRE_CHECK: 'preCheck',
  UPGRADE_MASTER: 'upgradeMaster',
  FINALIZE: 'finalize'
} as const;
export type ActiveAccordionId = (typeof ActiveAccordionId)[keyof typeof ActiveAccordionId];

const TSERVER_AZ_ACCORDION_ID_PREFIX = 'tserver:';
/** Stable accordion id for a per-AZ t-server upgrade stage. */
export const getTserverAzAccordionId = (azUUID: string, clusterUUID: string): string =>
  `${TSERVER_AZ_ACCORDION_ID_PREFIX}${getTserverAzClusterUpgradeStageKey(azUUID, clusterUUID)}`;

interface GetActiveDbUpgradeProgressAccordionIdInput {
  stages: DbUpgradeStagesMetadata;
  dbUpgradeTaskPauseState: CanaryPauseState | null | undefined;
  tserverAZUpgradeStatesList: AZUpgradeState[] | undefined;
  softwareUpgradeState: UniverseInfoSoftwareUpgradeState | undefined;
}

/**
 * Returns the id of the single "currently active" stage in the DB upgrade progress panel,
 * or null if none is active. Priority order (first match wins):
 *   1. Pre-check       - running or failed
 *   2. Upgrade master  - in progress, failed, or paused after a successful master upgrade
 *   3. Upgrade t-server AZ (in list order) - in progress, failed, or the last
 *      successfully upgraded AZ at a canary pause before remaining NOT_STARTED AZs.
 *      The first matching AZ in list order wins (so the first failure is the active one).
 *   4. Finalize        - universe is in the pre-finalize state
 */
export const getActiveDbUpgradeProgressAccordionId = ({
  stages,
  dbUpgradeTaskPauseState,
  tserverAZUpgradeStatesList,
  softwareUpgradeState
}: GetActiveDbUpgradeProgressAccordionIdInput): string | null => {
  if (
    stages.preCheckStage === AccordionCardState.IN_PROGRESS ||
    stages.preCheckStage === AccordionCardState.WARNING ||
    stages.preCheckStage === AccordionCardState.FAILED
  ) {
    return ActiveAccordionId.PRE_CHECK;
  }

  if (
    stages.upgradeMasterServersStage === AccordionCardState.IN_PROGRESS ||
    stages.upgradeMasterServersStage === AccordionCardState.FAILED ||
    (stages.upgradeMasterServersStage === AccordionCardState.SUCCESS &&
      dbUpgradeTaskPauseState === CanaryPauseState.PAUSED_AFTER_MASTERS)
  ) {
    return ActiveAccordionId.UPGRADE_MASTER;
  }

  for (const azUpgradeState of tserverAZUpgradeStatesList ?? []) {
    const stageKey = getTserverAzClusterUpgradeStageKey(
      azUpgradeState.azUUID,
      azUpgradeState.clusterUUID
    );
    const stageMetadata = stages.upgradeAzStages[stageKey];
    if (!stageMetadata) {
      continue;
    }
    const isPausedAfterSuccessfulUpgrade =
      stageMetadata.accordionCardState === AccordionCardState.SUCCESS &&
      dbUpgradeTaskPauseState === CanaryPauseState.PAUSED_AFTER_TSERVERS_AZ &&
      stageMetadata.isLastAzBeforeCanaryPause;
    if (
      stageMetadata.accordionCardState === AccordionCardState.IN_PROGRESS ||
      stageMetadata.accordionCardState === AccordionCardState.FAILED ||
      isPausedAfterSuccessfulUpgrade
    ) {
      return getTserverAzAccordionId(azUpgradeState.azUUID, azUpgradeState.clusterUUID);
    }
  }

  if (softwareUpgradeState === UniverseInfoSoftwareUpgradeState.PreFinalize) {
    return ActiveAccordionId.FINALIZE;
  }

  return null;
};
