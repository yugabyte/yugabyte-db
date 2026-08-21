import { describe, expect, expectTypeOf, it } from 'vitest';

import {
  createDbUpgradeMockAzUpgradeState,
  createMinimalSoftwareUpgradeProgressForTests
} from '@app/mocks/mock-data/taskMocks';
import {
  AZUpgradeStatus,
  CanaryPauseState,
  ServerType,
  TaskState,
  type SoftwareUpgradeProgress,
  type Task
} from '@app/redesign/features/tasks/dtos';
import { UniverseInfoSoftwareUpgradeState } from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';
import { AccordionCardState } from './AccordionCard';
import {
  ActiveAccordionId,
  classifyDbUpgradeStages,
  getActiveDbUpgradeProgressAccordionId,
  getTserverAzAccordionId,
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
    softwareUpgradeProgress: SoftwareUpgradeProgress | null | undefined,
    taskStatus?: TaskState
  ): Task =>
    ({
      details: {
        taskDetails: [],
        ...(softwareUpgradeProgress !== undefined && softwareUpgradeProgress !== null
          ? { softwareUpgradeProgress }
          : {})
      },
      ...(taskStatus !== undefined ? { status: taskStatus } : {})
    }) as unknown as Task;

  const noUniverseSoftwareUpgradeState = undefined;

  it('return type matches DbUpgradeStages', () => {
    expectTypeOf(
      classifyDbUpgradeStages(createDbUpgradeTask(null), noUniverseSoftwareUpgradeState)
    ).toEqualTypeOf<DbUpgradeStages>();
    expectTypeOf(
      classifyDbUpgradeStages(
        createDbUpgradeTask(createSoftwareUpgradeProgress()),
        noUniverseSoftwareUpgradeState
      )
    ).toEqualTypeOf<DbUpgradeStages>();
  });

  describe('when software upgrade progress is absent', () => {
    it('infers pre-check from task + universe; keeps other steps neutral', () => {
      for (const softwareUpgradeProgress of [null, undefined]) {
        const resultWithoutTaskStatus = classifyDbUpgradeStages(
          createDbUpgradeTask(softwareUpgradeProgress),
          noUniverseSoftwareUpgradeState
        );

        expect(resultWithoutTaskStatus.preCheckStage, String(softwareUpgradeProgress)).toBe(
          AccordionCardState.SUCCESS
        );
        expect(resultWithoutTaskStatus.upgradeMasterServersStage).toBe(AccordionCardState.NEUTRAL);
        expect(resultWithoutTaskStatus.upgradeAzStages).toEqual({});
        expect(resultWithoutTaskStatus.finalizeStage).toBe(AccordionCardState.NEUTRAL);
      }
    });

    it('maps RUNNING task + Ready universe to in-progress pre-check', () => {
      const result = classifyDbUpgradeStages(
        createDbUpgradeTask(null, TaskState.RUNNING),
        UniverseInfoSoftwareUpgradeState.Ready
      );
      expect(result.preCheckStage).toBe(AccordionCardState.IN_PROGRESS);
    });

    it('maps FAILURE task + Ready universe to failed pre-check (warning accordion)', () => {
      const result = classifyDbUpgradeStages(
        createDbUpgradeTask(null, TaskState.FAILURE),
        UniverseInfoSoftwareUpgradeState.Ready
      );
      expect(result.preCheckStage).toBe(AccordionCardState.WARNING);
    });
  });

  describe('pre-check stage with software upgrade progress present', () => {
    it('maps RUNNING task + Ready universe to in-progress pre-check regardless of AZ lists', () => {
      const result = classifyDbUpgradeStages(
        createDbUpgradeTask(
          createSoftwareUpgradeProgress({ masterAZUpgradeStatesList: [] }),
          TaskState.RUNNING
        ),
        UniverseInfoSoftwareUpgradeState.Ready
      );
      expect(result.preCheckStage).toBe(AccordionCardState.IN_PROGRESS);
    });

    it('maps FAILURE task + Ready universe to warning pre-check regardless of AZ lists', () => {
      const result = classifyDbUpgradeStages(
        createDbUpgradeTask(
          createSoftwareUpgradeProgress({ masterAZUpgradeStatesList: [] }),
          TaskState.FAILURE
        ),
        UniverseInfoSoftwareUpgradeState.Ready
      );
      expect(result.preCheckStage).toBe(AccordionCardState.WARNING);
    });
  });

  describe('master servers stage', () => {
    it('stays neutral when no master AZ rows are returned', () => {
      const result = classifyDbUpgradeStages(
        createDbUpgradeTask(createSoftwareUpgradeProgress({ masterAZUpgradeStatesList: [] })),
        noUniverseSoftwareUpgradeState
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
        ),
        noUniverseSoftwareUpgradeState
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
        ),
        noUniverseSoftwareUpgradeState
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
        ),
        noUniverseSoftwareUpgradeState
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
        ),
        noUniverseSoftwareUpgradeState
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
        ),
        noUniverseSoftwareUpgradeState
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
        ),
        noUniverseSoftwareUpgradeState
      );

      expect(result.upgradeAzStages[tserverStageKey(sharedAzUUID, primaryClusterUUID)]).toEqual({
        accordionCardState: AccordionCardState.IN_PROGRESS,
        isLastAzBeforeCanaryPause: false
      });
      expect(result.upgradeAzStages[tserverStageKey(sharedAzUUID, readReplicaClusterUUID)]).toEqual(
        {
          accordionCardState: AccordionCardState.NEUTRAL,
          isLastAzBeforeCanaryPause: false
        }
      );
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
          ),
          noUniverseSoftwareUpgradeState
        );

        expect(result.upgradeAzStages[tserverStageKey('single-az')]).toEqual({
          accordionCardState: expectedAccordionState,
          isLastAzBeforeCanaryPause: false
        });
      }
    );

    it('exposes no per-AZ entries when the t-server AZ list is empty', () => {
      const result = classifyDbUpgradeStages(
        createDbUpgradeTask(createSoftwareUpgradeProgress({ tserverAZUpgradeStatesList: [] })),
        noUniverseSoftwareUpgradeState
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
        ),
        noUniverseSoftwareUpgradeState
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
        ),
        noUniverseSoftwareUpgradeState
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
        ),
        noUniverseSoftwareUpgradeState
      );

      expect(result.upgradeAzStages[tserverStageKey('az-a')]?.isLastAzBeforeCanaryPause).toBe(
        false
      );
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
        ),
        noUniverseSoftwareUpgradeState
      );

      expect(result.upgradeAzStages[tserverStageKey('az-a')]?.isLastAzBeforeCanaryPause).toBe(
        false
      );
      expect(result.upgradeAzStages[tserverStageKey('az-b')]?.isLastAzBeforeCanaryPause).toBe(
        false
      );
    });
  });

  describe('finalize stage', () => {
    it('stays neutral until finalize classification is implemented', () => {
      const result = classifyDbUpgradeStages(
        createDbUpgradeTask(createSoftwareUpgradeProgress()),
        noUniverseSoftwareUpgradeState
      );

      expect(result.finalizeStage).toBe(AccordionCardState.NEUTRAL);
    });
  });
});

describe('getActiveDbUpgradeProgressAccordionId', () => {
  const clusterUUID = 'cluster-uuid';

  const tserverStageKey = (azUUID: string, clusterId: string = clusterUUID) =>
    getTserverAzClusterUpgradeStageKey(azUUID, clusterId);

  const createAzUpgradeState = (azUUID: string, status: AZUpgradeStatus, serverType: ServerType) =>
    createDbUpgradeMockAzUpgradeState(azUUID, status, serverType, clusterUUID);

  const createDbUpgradeTask = (
    softwareUpgradeProgress: SoftwareUpgradeProgress | null | undefined,
    taskStatus?: TaskState
  ): Task =>
    ({
      details: {
        taskDetails: [],
        ...(softwareUpgradeProgress !== undefined && softwareUpgradeProgress !== null
          ? { softwareUpgradeProgress }
          : {})
      },
      ...(taskStatus !== undefined ? { status: taskStatus } : {})
    }) as unknown as Task;

  it('returns pre-check id when pre-check is running', () => {
    const task = createDbUpgradeTask(null, TaskState.RUNNING);
    const stages = classifyDbUpgradeStages(task, UniverseInfoSoftwareUpgradeState.Ready);

    expect(
      getActiveDbUpgradeProgressAccordionId({
        stages,
        dbUpgradeTaskPauseState: undefined,
        tserverAZUpgradeStatesList: undefined,
        softwareUpgradeState: UniverseInfoSoftwareUpgradeState.Ready
      })
    ).toBe(ActiveAccordionId.PRE_CHECK);
  });

  it('returns pre-check id when pre-check has failed (warning state)', () => {
    const task = createDbUpgradeTask(null, TaskState.FAILURE);
    const stages = classifyDbUpgradeStages(task, UniverseInfoSoftwareUpgradeState.Ready);

    expect(stages.preCheckStage).toBe(AccordionCardState.WARNING);
    expect(
      getActiveDbUpgradeProgressAccordionId({
        stages,
        dbUpgradeTaskPauseState: undefined,
        tserverAZUpgradeStatesList: undefined,
        softwareUpgradeState: UniverseInfoSoftwareUpgradeState.Ready
      })
    ).toBe(ActiveAccordionId.PRE_CHECK);
  });

  it('prefers pre-check over later stages when pre-check is running', () => {
    // Pre-check running, master AZs also in progress; pre-check still wins.
    const softwareUpgradeProgress = createMinimalSoftwareUpgradeProgressForTests({
      masterAZUpgradeStatesList: [
        createAzUpgradeState('az-1', AZUpgradeStatus.IN_PROGRESS, ServerType.MASTER)
      ]
    });
    const task = createDbUpgradeTask(softwareUpgradeProgress, TaskState.RUNNING);
    const stages = classifyDbUpgradeStages(task, UniverseInfoSoftwareUpgradeState.Ready);

    expect(
      getActiveDbUpgradeProgressAccordionId({
        stages,
        dbUpgradeTaskPauseState: softwareUpgradeProgress.canaryPauseState,
        tserverAZUpgradeStatesList: softwareUpgradeProgress.tserverAZUpgradeStatesList,
        softwareUpgradeState: UniverseInfoSoftwareUpgradeState.Ready
      })
    ).toBe(ActiveAccordionId.PRE_CHECK);
  });

  it('returns upgrade-master id when master upgrade is in progress', () => {
    const softwareUpgradeProgress = createMinimalSoftwareUpgradeProgressForTests({
      masterAZUpgradeStatesList: [
        createAzUpgradeState('az-1', AZUpgradeStatus.IN_PROGRESS, ServerType.MASTER)
      ]
    });
    const task = createDbUpgradeTask(softwareUpgradeProgress);
    const stages = classifyDbUpgradeStages(task, undefined);

    expect(
      getActiveDbUpgradeProgressAccordionId({
        stages,
        dbUpgradeTaskPauseState: softwareUpgradeProgress.canaryPauseState,
        tserverAZUpgradeStatesList: softwareUpgradeProgress.tserverAZUpgradeStatesList,
        softwareUpgradeState: undefined
      })
    ).toBe(ActiveAccordionId.UPGRADE_MASTER);
  });

  it('returns upgrade-master id when the master upgrade has failed', () => {
    const softwareUpgradeProgress = createMinimalSoftwareUpgradeProgressForTests({
      masterAZUpgradeStatesList: [
        createAzUpgradeState('az-1', AZUpgradeStatus.IN_PROGRESS, ServerType.MASTER),
        createAzUpgradeState('az-2', AZUpgradeStatus.FAILED, ServerType.MASTER)
      ]
    });
    const task = createDbUpgradeTask(softwareUpgradeProgress);
    const stages = classifyDbUpgradeStages(task, undefined);

    expect(stages.upgradeMasterServersStage).toBe(AccordionCardState.FAILED);
    expect(
      getActiveDbUpgradeProgressAccordionId({
        stages,
        dbUpgradeTaskPauseState: softwareUpgradeProgress.canaryPauseState,
        tserverAZUpgradeStatesList: softwareUpgradeProgress.tserverAZUpgradeStatesList,
        softwareUpgradeState: undefined
      })
    ).toBe(ActiveAccordionId.UPGRADE_MASTER);
  });

  it('returns upgrade-master id when paused after a successful master upgrade', () => {
    const softwareUpgradeProgress = createMinimalSoftwareUpgradeProgressForTests({
      canaryPauseState: CanaryPauseState.PAUSED_AFTER_MASTERS,
      masterAZUpgradeStatesList: [
        createAzUpgradeState('az-1', AZUpgradeStatus.COMPLETED, ServerType.MASTER),
        createAzUpgradeState('az-2', AZUpgradeStatus.COMPLETED, ServerType.MASTER)
      ],
      tserverAZUpgradeStatesList: [
        createAzUpgradeState('az-1', AZUpgradeStatus.NOT_STARTED, ServerType.TSERVER)
      ]
    });
    const task = createDbUpgradeTask(softwareUpgradeProgress);
    const stages = classifyDbUpgradeStages(task, undefined);

    expect(stages.upgradeMasterServersStage).toBe(AccordionCardState.SUCCESS);
    expect(
      getActiveDbUpgradeProgressAccordionId({
        stages,
        dbUpgradeTaskPauseState: softwareUpgradeProgress.canaryPauseState,
        tserverAZUpgradeStatesList: softwareUpgradeProgress.tserverAZUpgradeStatesList,
        softwareUpgradeState: undefined
      })
    ).toBe(ActiveAccordionId.UPGRADE_MASTER);
  });

  it('returns the first in-progress t-server AZ id in list order', () => {
    const softwareUpgradeProgress = createMinimalSoftwareUpgradeProgressForTests({
      masterAZUpgradeStatesList: [
        createAzUpgradeState('az-1', AZUpgradeStatus.COMPLETED, ServerType.MASTER)
      ],
      tserverAZUpgradeStatesList: [
        createAzUpgradeState('az-first', AZUpgradeStatus.COMPLETED, ServerType.TSERVER),
        createAzUpgradeState('az-middle', AZUpgradeStatus.IN_PROGRESS, ServerType.TSERVER),
        createAzUpgradeState('az-last', AZUpgradeStatus.NOT_STARTED, ServerType.TSERVER)
      ]
    });
    const task = createDbUpgradeTask(softwareUpgradeProgress);
    const stages = classifyDbUpgradeStages(task, undefined);

    expect(
      getActiveDbUpgradeProgressAccordionId({
        stages,
        dbUpgradeTaskPauseState: softwareUpgradeProgress.canaryPauseState,
        tserverAZUpgradeStatesList: softwareUpgradeProgress.tserverAZUpgradeStatesList,
        softwareUpgradeState: undefined
      })
    ).toBe(getTserverAzAccordionId('az-middle', clusterUUID));
  });

  it('returns the first failed t-server AZ id in list order', () => {
    const softwareUpgradeProgress = createMinimalSoftwareUpgradeProgressForTests({
      masterAZUpgradeStatesList: [
        createAzUpgradeState('az-1', AZUpgradeStatus.COMPLETED, ServerType.MASTER)
      ],
      tserverAZUpgradeStatesList: [
        createAzUpgradeState('az-first', AZUpgradeStatus.COMPLETED, ServerType.TSERVER),
        createAzUpgradeState('az-failed-1', AZUpgradeStatus.FAILED, ServerType.TSERVER),
        createAzUpgradeState('az-failed-2', AZUpgradeStatus.FAILED, ServerType.TSERVER),
        createAzUpgradeState('az-rest', AZUpgradeStatus.NOT_STARTED, ServerType.TSERVER)
      ]
    });
    const task = createDbUpgradeTask(softwareUpgradeProgress);
    const stages = classifyDbUpgradeStages(task, undefined);

    expect(
      getActiveDbUpgradeProgressAccordionId({
        stages,
        dbUpgradeTaskPauseState: softwareUpgradeProgress.canaryPauseState,
        tserverAZUpgradeStatesList: softwareUpgradeProgress.tserverAZUpgradeStatesList,
        softwareUpgradeState: undefined
      })
    ).toBe(getTserverAzAccordionId('az-failed-1', clusterUUID));
  });

  it('returns the canary-pause boundary t-server AZ id when paused after t-servers', () => {
    const softwareUpgradeProgress = createMinimalSoftwareUpgradeProgressForTests({
      canaryPauseState: CanaryPauseState.PAUSED_AFTER_TSERVERS_AZ,
      masterAZUpgradeStatesList: [
        createAzUpgradeState('az-1', AZUpgradeStatus.COMPLETED, ServerType.MASTER)
      ],
      tserverAZUpgradeStatesList: [
        createAzUpgradeState('az-first', AZUpgradeStatus.COMPLETED, ServerType.TSERVER),
        createAzUpgradeState('az-boundary', AZUpgradeStatus.COMPLETED, ServerType.TSERVER),
        createAzUpgradeState('az-rest', AZUpgradeStatus.NOT_STARTED, ServerType.TSERVER)
      ]
    });
    const task = createDbUpgradeTask(softwareUpgradeProgress);
    const stages = classifyDbUpgradeStages(task, undefined);

    expect(stages.upgradeAzStages[tserverStageKey('az-boundary')]?.isLastAzBeforeCanaryPause).toBe(
      true
    );
    expect(
      getActiveDbUpgradeProgressAccordionId({
        stages,
        dbUpgradeTaskPauseState: softwareUpgradeProgress.canaryPauseState,
        tserverAZUpgradeStatesList: softwareUpgradeProgress.tserverAZUpgradeStatesList,
        softwareUpgradeState: undefined
      })
    ).toBe(getTserverAzAccordionId('az-boundary', clusterUUID));
  });

  it('returns finalize id when the universe is in pre-finalize state and no earlier stage is active', () => {
    const softwareUpgradeProgress = createMinimalSoftwareUpgradeProgressForTests({
      masterAZUpgradeStatesList: [
        createAzUpgradeState('az-1', AZUpgradeStatus.COMPLETED, ServerType.MASTER)
      ],
      tserverAZUpgradeStatesList: [
        createAzUpgradeState('az-1', AZUpgradeStatus.COMPLETED, ServerType.TSERVER)
      ]
    });
    const task = createDbUpgradeTask(softwareUpgradeProgress);
    const stages = classifyDbUpgradeStages(task, UniverseInfoSoftwareUpgradeState.PreFinalize);

    expect(
      getActiveDbUpgradeProgressAccordionId({
        stages,
        dbUpgradeTaskPauseState: softwareUpgradeProgress.canaryPauseState,
        tserverAZUpgradeStatesList: softwareUpgradeProgress.tserverAZUpgradeStatesList,
        softwareUpgradeState: UniverseInfoSoftwareUpgradeState.PreFinalize
      })
    ).toBe(ActiveAccordionId.FINALIZE);
  });

  it('returns null when nothing is currently active', () => {
    // All AZs not started, no pre-check progress, no finalize → no stage demands focus.
    const softwareUpgradeProgress = createMinimalSoftwareUpgradeProgressForTests({
      masterAZUpgradeStatesList: [
        createAzUpgradeState('az-1', AZUpgradeStatus.NOT_STARTED, ServerType.MASTER)
      ],
      tserverAZUpgradeStatesList: [
        createAzUpgradeState('az-1', AZUpgradeStatus.NOT_STARTED, ServerType.TSERVER)
      ]
    });
    const task = createDbUpgradeTask(softwareUpgradeProgress);
    const stages = classifyDbUpgradeStages(task, undefined);

    expect(
      getActiveDbUpgradeProgressAccordionId({
        stages,
        dbUpgradeTaskPauseState: softwareUpgradeProgress.canaryPauseState,
        tserverAZUpgradeStatesList: softwareUpgradeProgress.tserverAZUpgradeStatesList,
        softwareUpgradeState: undefined
      })
    ).toBeNull();
  });

  it('does not treat a t-server AZ as active when paused after masters even if last AZ flag is false', () => {
    // Master pause + earlier completed tservers but pause state is AFTER_MASTERS, not tservers.
    const softwareUpgradeProgress = createMinimalSoftwareUpgradeProgressForTests({
      canaryPauseState: CanaryPauseState.PAUSED_AFTER_MASTERS,
      masterAZUpgradeStatesList: [
        createAzUpgradeState('az-1', AZUpgradeStatus.COMPLETED, ServerType.MASTER)
      ],
      tserverAZUpgradeStatesList: [
        createAzUpgradeState('az-tserver-1', AZUpgradeStatus.NOT_STARTED, ServerType.TSERVER)
      ]
    });
    const task = createDbUpgradeTask(softwareUpgradeProgress);
    const stages = classifyDbUpgradeStages(task, undefined);

    expect(
      getActiveDbUpgradeProgressAccordionId({
        stages,
        dbUpgradeTaskPauseState: softwareUpgradeProgress.canaryPauseState,
        tserverAZUpgradeStatesList: softwareUpgradeProgress.tserverAZUpgradeStatesList,
        softwareUpgradeState: undefined
      })
    ).toBe(ActiveAccordionId.UPGRADE_MASTER);
  });
});
