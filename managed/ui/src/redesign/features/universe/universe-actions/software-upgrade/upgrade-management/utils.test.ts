import { describe, expect, expectTypeOf, it } from 'vitest';

import {
  createDbUpgradeMockAzUpgradeState,
  createMinimalSoftwareUpgradeProgressForTests
} from '@app/mocks/mock-data/taskMocks';
import {
  AZUpgradeStatus,
  CanaryPauseState,
  DbUpgradePrecheckStatus,
  ServerType,
  type SoftwareUpgradeProgress,
  type Task
} from '@app/redesign/features/tasks/dtos';
import { AccordionCardState } from './AccordionCard';
import {
  classifyDbUpgradeStages,
  getTserverAzClusterUpgradeStageKey,
  type DbUpgradeStages
} from './utils';

describe('classifyDbUpgradeStages', () => {
  const clusterUUID = 'cluster-uuid';

  const tserverStageKey = (azUUID: string, clusterId: string = clusterUUID) =>
    getTserverAzClusterUpgradeStageKey(azUUID, clusterId);

  const createSoftwareUpgradeProgress = (
    partial: Partial<SoftwareUpgradeProgress> = {}
  ): SoftwareUpgradeProgress => createMinimalSoftwareUpgradeProgressForTests(partial);

  const createAzUpgradeState = (azUUID: string, status: AZUpgradeStatus, serverType: ServerType) =>
    createDbUpgradeMockAzUpgradeState(azUUID, status, serverType, clusterUUID);

  const createDbUpgradeTask = (
    softwareUpgradeProgress: SoftwareUpgradeProgress | null | undefined
  ): Task => ({ softwareUpgradeProgress }) as Task;

  it('return type matches DbUpgradeStages', () => {
    expectTypeOf(
      classifyDbUpgradeStages(createDbUpgradeTask(null))
    ).toEqualTypeOf<DbUpgradeStages>();
    expectTypeOf(
      classifyDbUpgradeStages(createDbUpgradeTask(createSoftwareUpgradeProgress()))
    ).toEqualTypeOf<DbUpgradeStages>();
  });

  describe('when software upgrade progress is absent', () => {
    it('treats every step as idle so the panel does not show false success or failure', () => {
      for (const softwareUpgradeProgress of [null, undefined]) {
        const result = classifyDbUpgradeStages(createDbUpgradeTask(softwareUpgradeProgress));

        expect(result.preCheckStage, String(softwareUpgradeProgress)).toBe(
          AccordionCardState.NEUTRAL
        );
        expect(result.upgradeMasterServersStage).toBe(AccordionCardState.NEUTRAL);
        expect(result.upgradeAzStages).toEqual({});
        expect(result.finalizeStage).toBe(AccordionCardState.NEUTRAL);
      }
    });
  });

  describe('precheck stage', () => {
    it.each([
      [DbUpgradePrecheckStatus.SUCCESS, AccordionCardState.SUCCESS],
      [DbUpgradePrecheckStatus.RUNNING, AccordionCardState.IN_PROGRESS],
      [DbUpgradePrecheckStatus.FAILED, AccordionCardState.WARNING]
    ] as const)(
      'when precheck status is %s, the precheck stage shows %s',
      (precheckStatus: DbUpgradePrecheckStatus, expectedAccordionState: AccordionCardState) => {
        const result = classifyDbUpgradeStages(
          createDbUpgradeTask(createSoftwareUpgradeProgress({ precheckStatus }))
        );

        expect(result.preCheckStage).toBe(expectedAccordionState);
      }
    );

    it('when precheck status is unrecognized, the precheck stage returns as neutral', () => {
      const resultUnknownString = classifyDbUpgradeStages(
        createDbUpgradeTask(
          createSoftwareUpgradeProgress({
            precheckStatus: 'unknown' as DbUpgradePrecheckStatus
          })
        )
      );
      expect(resultUnknownString.preCheckStage).toBe(AccordionCardState.NEUTRAL);
    });
  });

  describe('master servers stage', () => {
    it('stays neutral when no master AZ rows are returned', () => {
      const result = classifyDbUpgradeStages(
        createDbUpgradeTask(createSoftwareUpgradeProgress({ masterAZUpgradeStatesList: [] }))
      );

      expect(result.upgradeMasterServersStage).toBe(AccordionCardState.NEUTRAL);
    });

    it('counts as in progress when some master AZs are completed and others are not started yet', () => {
      const result = classifyDbUpgradeStages(
        createDbUpgradeTask(
          createSoftwareUpgradeProgress({
            masterAZUpgradeStatesList: [
              createAzUpgradeState('az-1', AZUpgradeStatus.NOT_STARTED, ServerType.MASTER),
              createAzUpgradeState('az-2', AZUpgradeStatus.COMPLETED, ServerType.MASTER)
            ]
          })
        )
      );

      expect(result.upgradeMasterServersStage).toBe(AccordionCardState.IN_PROGRESS);
    });

    it('returns success only when every master AZ has completed', () => {
      const result = classifyDbUpgradeStages(
        createDbUpgradeTask(
          createSoftwareUpgradeProgress({
            masterAZUpgradeStatesList: [
              createAzUpgradeState('az-1', AZUpgradeStatus.COMPLETED, ServerType.MASTER),
              createAzUpgradeState('az-2', AZUpgradeStatus.COMPLETED, ServerType.MASTER)
            ]
          })
        )
      );

      expect(result.upgradeMasterServersStage).toBe(AccordionCardState.SUCCESS);
    });

    it('returns in progress when at least one master AZ is upgrading and none have failed', () => {
      const result = classifyDbUpgradeStages(
        createDbUpgradeTask(
          createSoftwareUpgradeProgress({
            masterAZUpgradeStatesList: [
              createAzUpgradeState('az-1', AZUpgradeStatus.COMPLETED, ServerType.MASTER),
              createAzUpgradeState('az-2', AZUpgradeStatus.IN_PROGRESS, ServerType.MASTER)
            ]
          })
        )
      );

      expect(result.upgradeMasterServersStage).toBe(AccordionCardState.IN_PROGRESS);
    });

    it('returns failed when any master AZ reports failure, even if others are still upgrading', () => {
      const result = classifyDbUpgradeStages(
        createDbUpgradeTask(
          createSoftwareUpgradeProgress({
            masterAZUpgradeStatesList: [
              createAzUpgradeState('az-1', AZUpgradeStatus.IN_PROGRESS, ServerType.MASTER),
              createAzUpgradeState('az-2', AZUpgradeStatus.FAILED, ServerType.MASTER)
            ]
          })
        )
      );

      expect(result.upgradeMasterServersStage).toBe(AccordionCardState.FAILED);
    });
  });

  describe('per-AZ t-server stages', () => {
    it('keeps each AZ independent: two AZs can show different step states at once', () => {
      const result = classifyDbUpgradeStages(
        createDbUpgradeTask(
          createSoftwareUpgradeProgress({
            tserverAZUpgradeStatesList: [
              createAzUpgradeState('az-west', AZUpgradeStatus.IN_PROGRESS, ServerType.TSERVER),
              createAzUpgradeState('az-east', AZUpgradeStatus.COMPLETED, ServerType.TSERVER)
            ]
          })
        )
      );

      expect(result.upgradeAzStages[tserverStageKey('az-west')]).toEqual({
        accordionCardState: AccordionCardState.IN_PROGRESS,
        isLastAzBeforeCanaryPause: false
      });
      expect(result.upgradeAzStages[tserverStageKey('az-east')]).toEqual({
        accordionCardState: AccordionCardState.SUCCESS,
        isLastAzBeforeCanaryPause: false
      });
    });

    it('keeps each AZ+cluster row independent when the same AZ UUID appears on multiple clusters', () => {
      const primaryClusterUUID = 'primary-cluster';
      const readReplicaClusterUUID = 'read-replica-cluster';
      const sharedAzUUID = 'shared-az-uuid';

      const result = classifyDbUpgradeStages(
        createDbUpgradeTask(
          createSoftwareUpgradeProgress({
            tserverAZUpgradeStatesList: [
              createDbUpgradeMockAzUpgradeState(
                sharedAzUUID,
                AZUpgradeStatus.IN_PROGRESS,
                ServerType.TSERVER,
                primaryClusterUUID
              ),
              createDbUpgradeMockAzUpgradeState(
                sharedAzUUID,
                AZUpgradeStatus.NOT_STARTED,
                ServerType.TSERVER,
                readReplicaClusterUUID
              )
            ]
          })
        )
      );

      expect(result.upgradeAzStages[tserverStageKey(sharedAzUUID, primaryClusterUUID)]).toEqual({
        accordionCardState: AccordionCardState.IN_PROGRESS,
        isLastAzBeforeCanaryPause: false
      });
      expect(result.upgradeAzStages[tserverStageKey(sharedAzUUID, readReplicaClusterUUID)]).toEqual({
        accordionCardState: AccordionCardState.NEUTRAL,
        isLastAzBeforeCanaryPause: false
      });
    });

    it.each([
      [AZUpgradeStatus.NOT_STARTED, AccordionCardState.NEUTRAL],
      [AZUpgradeStatus.IN_PROGRESS, AccordionCardState.IN_PROGRESS],
      [AZUpgradeStatus.COMPLETED, AccordionCardState.SUCCESS],
      [AZUpgradeStatus.FAILED, AccordionCardState.FAILED]
    ] as const)(
      'for a single t-server AZ, backend status %s maps the stage state to %s',
      (azStatus: AZUpgradeStatus, expectedAccordionState: AccordionCardState) => {
        const result = classifyDbUpgradeStages(
          createDbUpgradeTask(
            createSoftwareUpgradeProgress({
              tserverAZUpgradeStatesList: [
                createAzUpgradeState('single-az', azStatus, ServerType.TSERVER)
              ]
            })
          )
        );

        expect(result.upgradeAzStages[tserverStageKey('single-az')]).toEqual({
          accordionCardState: expectedAccordionState,
          isLastAzBeforeCanaryPause: false
        });
      }
    );

    it('exposes no per-AZ entries when the t-server AZ list is empty', () => {
      const result = classifyDbUpgradeStages(
        createDbUpgradeTask(createSoftwareUpgradeProgress({ tserverAZUpgradeStatesList: [] }))
      );

      expect(result.upgradeAzStages).toEqual({});
    });

    it('treats an unrecognized AZ status as neutral so a bad payload does not break the panel', () => {
      const result = classifyDbUpgradeStages(
        createDbUpgradeTask(
          createSoftwareUpgradeProgress({
            tserverAZUpgradeStatesList: [
              {
                ...createAzUpgradeState('az-x', AZUpgradeStatus.COMPLETED, ServerType.TSERVER),
                status: 'UNKNOWN_STATUS' as AZUpgradeStatus
              }
            ]
          })
        )
      );

      expect(result.upgradeAzStages[tserverStageKey('az-x')]).toEqual({
        accordionCardState: AccordionCardState.NEUTRAL,
        isLastAzBeforeCanaryPause: false
      });
    });

    it('marks the ordered last completed AZ when paused after t-servers with remaining NOT_STARTED', () => {
      const result = classifyDbUpgradeStages(
        createDbUpgradeTask(
          createSoftwareUpgradeProgress({
            canaryPauseState: CanaryPauseState.PAUSED_AFTER_TSERVERS_AZ,
            tserverAZUpgradeStatesList: [
              createAzUpgradeState('az-first', AZUpgradeStatus.COMPLETED, ServerType.TSERVER),
              createAzUpgradeState('az-boundary', AZUpgradeStatus.COMPLETED, ServerType.TSERVER),
              createAzUpgradeState('az-rest', AZUpgradeStatus.NOT_STARTED, ServerType.TSERVER)
            ]
          })
        )
      );

      expect(result.upgradeAzStages[tserverStageKey('az-first')]).toEqual({
        accordionCardState: AccordionCardState.SUCCESS,
        isLastAzBeforeCanaryPause: false
      });
      expect(result.upgradeAzStages[tserverStageKey('az-boundary')]).toEqual({
        accordionCardState: AccordionCardState.SUCCESS,
        isLastAzBeforeCanaryPause: true
      });
      expect(result.upgradeAzStages[tserverStageKey('az-rest')]).toEqual({
        accordionCardState: AccordionCardState.NEUTRAL,
        isLastAzBeforeCanaryPause: false
      });
    });

    it('does not mark a pause boundary when pause state is not after t-servers', () => {
      const result = classifyDbUpgradeStages(
        createDbUpgradeTask(
          createSoftwareUpgradeProgress({
            canaryPauseState: CanaryPauseState.PAUSED_AFTER_MASTERS,
            tserverAZUpgradeStatesList: [
              createAzUpgradeState('az-a', AZUpgradeStatus.COMPLETED, ServerType.TSERVER),
              createAzUpgradeState('az-b', AZUpgradeStatus.NOT_STARTED, ServerType.TSERVER)
            ]
          })
        )
      );

      expect(result.upgradeAzStages[tserverStageKey('az-a')]?.isLastAzBeforeCanaryPause).toBe(false);
    });

    it('does not mark a pause boundary when every t-server AZ has completed', () => {
      const result = classifyDbUpgradeStages(
        createDbUpgradeTask(
          createSoftwareUpgradeProgress({
            canaryPauseState: CanaryPauseState.PAUSED_AFTER_TSERVERS_AZ,
            tserverAZUpgradeStatesList: [
              createAzUpgradeState('az-a', AZUpgradeStatus.COMPLETED, ServerType.TSERVER),
              createAzUpgradeState('az-b', AZUpgradeStatus.COMPLETED, ServerType.TSERVER)
            ]
          })
        )
      );

      expect(result.upgradeAzStages[tserverStageKey('az-a')]?.isLastAzBeforeCanaryPause).toBe(false);
      expect(result.upgradeAzStages[tserverStageKey('az-b')]?.isLastAzBeforeCanaryPause).toBe(false);
    });
  });

  describe('finalize stage', () => {
    it('stays neutral until finalize classification is implemented', () => {
      const result = classifyDbUpgradeStages(createDbUpgradeTask(createSoftwareUpgradeProgress()));

      expect(result.finalizeStage).toBe(AccordionCardState.NEUTRAL);
    });
  });
});
