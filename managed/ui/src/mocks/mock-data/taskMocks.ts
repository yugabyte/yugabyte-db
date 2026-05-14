import {
  AZUpgradeState,
  AZUpgradeStatus,
  CanaryPauseState,
  SoftwareUpgradeProgress,
  DbUpgradePrecheckStatus,
  ServerType,
  Task,
  TaskState,
  TaskType,
  TargetType
} from '@app/redesign/features/tasks/dtos';
import type { UniverseSoftwareUpgradePrecheckResp } from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';

const TASK_UUID = 'a1b2c3d4-e5f6-7890-abcd-ef1234567890';
export const DB_UPGRADE_TASK_MOCK_UNIVERSE_UUID = 'mock-universe-uuid';
const PRIMARY_CLUSTER_UUID = 'e5f6a7b8-c9d0-1234-ef01-345678901234';

/** SubTaskGroupType.PreflightChecks — not a CustomerTask type; asserted for SubTaskInfo typing. */
const PREFLIGHT_SUBTASK_GROUP = 'PreflightChecks' as Task['type'];

export const createDbUpgradeMockAzUpgradeState = (
  azUUID: string,
  status: AZUpgradeStatus,
  serverType: ServerType,
  clusterUUID: string,
  azName?: string
): AZUpgradeState => ({
  azUUID,
  azName: azName ?? azUUID,
  serverType,
  clusterUUID,
  status
});

/**
 * Minimal software upgrade progress for unit tests (valid {@link SoftwareUpgradeProgress} without assertions).
 * Spread `partial` to override fields.
 */
export const createMinimalSoftwareUpgradeProgressForTests = (
  partial: Partial<SoftwareUpgradeProgress> = {}
): SoftwareUpgradeProgress => ({
  canaryUpgrade: true,
  canaryPauseState: CanaryPauseState.NOT_PAUSED,
  precheckStatus: DbUpgradePrecheckStatus.RUNNING,
  masterAZUpgradeStatesList: [],
  tserverAZUpgradeStatesList: [],
  ...partial
});

export const defaultDbUpgradeSoftwareUpgradeProgress = (): SoftwareUpgradeProgress => ({
  canaryUpgrade: true,
  canaryPauseState: CanaryPauseState.NOT_PAUSED,
  precheckStatus: DbUpgradePrecheckStatus.RUNNING,
  masterAZUpgradeStatesList: [
    createDbUpgradeMockAzUpgradeState(
      'us-west-2a-uuid',
      AZUpgradeStatus.NOT_STARTED,
      ServerType.MASTER,
      PRIMARY_CLUSTER_UUID,
      'us-west-2a'
    ),
    createDbUpgradeMockAzUpgradeState(
      'us-west-2b-uuid',
      AZUpgradeStatus.NOT_STARTED,
      ServerType.MASTER,
      PRIMARY_CLUSTER_UUID,
      'us-west-2b'
    ),
    createDbUpgradeMockAzUpgradeState(
      'sa-east-1a-uuid',
      AZUpgradeStatus.NOT_STARTED,
      ServerType.MASTER,
      PRIMARY_CLUSTER_UUID,
      'sa-east-1a'
    )
  ],
  tserverAZUpgradeStatesList: [
    createDbUpgradeMockAzUpgradeState(
      'us-west-2a-uuid',
      AZUpgradeStatus.NOT_STARTED,
      ServerType.TSERVER,
      PRIMARY_CLUSTER_UUID,
      'us-west-2a'
    ),
    createDbUpgradeMockAzUpgradeState(
      'us-west-2b-uuid',
      AZUpgradeStatus.NOT_STARTED,
      ServerType.TSERVER,
      PRIMARY_CLUSTER_UUID,
      'us-west-2b'
    ),
    createDbUpgradeMockAzUpgradeState(
      'sa-east-1a-uuid',
      AZUpgradeStatus.NOT_STARTED,
      ServerType.TSERVER,
      PRIMARY_CLUSTER_UUID,
      'sa-east-1a'
    )
  ]
});

/**
 * Returns tserver AZ rows with the second-to-last AZ in progress and the last not started
 * (earlier AZs completed). Used in Storybook and tests to exercise the full AZ upgrade span.
 */
export const tserverAzUpgradeStatesListWithSecondLastInProgress = <
  T extends { status: AZUpgradeStatus }
>(
  tserverAZUpgradeStatesList: T[]
): T[] => {
  const tserverCount = tserverAZUpgradeStatesList.length;
  return tserverAZUpgradeStatesList.map((az, index) => {
    if (tserverCount === 1) {
      return { ...az, status: AZUpgradeStatus.IN_PROGRESS };
    }
    if (index < tserverCount - 2) {
      return { ...az, status: AZUpgradeStatus.COMPLETED };
    }
    if (index === tserverCount - 2) {
      return { ...az, status: AZUpgradeStatus.IN_PROGRESS };
    }
    return { ...az, status: AZUpgradeStatus.NOT_STARTED };
  });
};

type CreateDbUpgradeTaskMockOverrides = Partial<Task> & {
  softwareUpgradeProgress?: Partial<SoftwareUpgradeProgress>;
};

const buildBaseDbUpgradeTask = (): Task => ({
  id: TASK_UUID,
  title: 'Upgrade Software : yb-demo-universe',
  percentComplete: 65,
  createTime: '2025-03-15T14:22:33Z',
  completionTime: '2025-03-15T19:45:12Z',
  target: TargetType.UNIVERSE as Task['target'],
  targetUUID: DB_UPGRADE_TASK_MOCK_UNIVERSE_UUID,
  type: TaskType.SOFTWARE_UPGRADE as Task['type'],
  typeName: 'Software Upgrade',
  status: TaskState.PAUSED,
  details: {
    taskDetails: [
      {
        title: 'Preflight Checks',
        description: 'Perform preflight checks to determine if the task target is healthy.',
        state: TaskState.SUCCESS,
        extraDetails: []
      },
      {
        title: 'Downloading software',
        description: 'Downloading the YugaByte software on provisioned nodes.',
        state: TaskState.SUCCESS,
        extraDetails: []
      },
      {
        title: 'Upgrading software',
        description: 'Upgrading YugaByte software on existing clusters.',
        state: TaskState.RUNNING,
        extraDetails: []
      },
      {
        title: 'Finalizing upgrade',
        description: 'Finalizing Yugabyte DB Software version upgrade on universe',
        state: TaskState.UNKNOWN,
        extraDetails: []
      }
    ],
    versionNumbers: {
      ybPrevSoftwareVersion: '2024.2.1.0-b123',
      ybSoftwareVersion: '2024.2.2.0-b456'
    },
    softwareUpgradeProgress: defaultDbUpgradeSoftwareUpgradeProgress()
  },
  abortable: true,
  retryable: false,
  canRollback: true,
  correlationId: 'corr-7f3a9b2e-software-upgrade',
  userEmail: 'operator@example.com',
  subtaskInfos: [
    {
      uuid: 'b2c3d4e5-f6a7-8901-bcde-f01234567890',
      parentUuid: TASK_UUID,
      taskType: 'CheckUpgrade',
      taskState: TaskState.SUCCESS,
      subTaskGroupType: PREFLIGHT_SUBTASK_GROUP,
      createTime: 1710512400000,
      updateTime: 1710512410000,
      percentDone: 100,
      position: 0,
      taskParams: {}
    },
    {
      uuid: 'c3d4e5f6-a7b8-9012-cdef-123456789012',
      parentUuid: TASK_UUID,
      taskType: 'UpdateSoftwareVersion',
      taskState: TaskState.SUCCESS,
      subTaskGroupType: 'UpgradingSoftware' as Task['type'],
      createTime: 1710512553000,
      updateTime: 1710512600000,
      percentDone: 100,
      position: 1,
      taskParams: {
        nodeName: 'yb-demo-universe-n1'
      }
    },
    {
      uuid: 'd4e5f6a7-b8c9-0123-def0-234567890123',
      parentUuid: TASK_UUID,
      taskType: 'CheckSoftwareVersion',
      taskState: TaskState.RUNNING,
      subTaskGroupType: 'UpgradingSoftware' as Task['type'],
      createTime: 1710512600000,
      updateTime: 1710512650000,
      percentDone: 45,
      position: 2,
      taskParams: {
        nodeName: 'yb-demo-universe-n2'
      }
    }
  ],
  taskInfo: {
    taskParams: {}
  }
});

export const createDbUpgradeTaskMock = (overrides: CreateDbUpgradeTaskMockOverrides = {}): Task => {
  const base = buildBaseDbUpgradeTask();
  const { softwareUpgradeProgress: progressPartial, details: detailsOverride, ...taskPartial } =
    overrides;
  return {
    ...base,
    ...taskPartial,
    details: {
      ...base.details,
      ...detailsOverride,
      ...(progressPartial !== undefined && {
        softwareUpgradeProgress: {
          ...base.details.softwareUpgradeProgress!,
          ...progressPartial
        }
      })
    }
  };
};

export const DB_UPGRADE_PRECHECK_VALIDATION_TASK_ID =
  'a0000001-0001-4000-8000-000000000001';

export const DB_UPGRADE_PRECHECK_VALIDATION_TASK_UNIVERSE_UUID =
  'a0000001-0001-4000-8000-000000000002';

const buildDbUpgradePrecheckValidationTaskFixture = (): Task => ({
  id: DB_UPGRADE_PRECHECK_VALIDATION_TASK_ID,
  title: 'Upgraded Software Universe : mock-universe',
  percentComplete: 91,
  createTime: '2026-04-20T21:53:38Z',
  completionTime: '2026-04-20T21:53:42Z',
  target: TargetType.UNIVERSE as Task['target'],
  targetUUID: DB_UPGRADE_PRECHECK_VALIDATION_TASK_UNIVERSE_UUID,
  type: TaskType.SOFTWARE_UPGRADE as Task['type'],
  typeName: 'Validation Software Upgrade',
  status: TaskState.FAILURE,
  details: {
    taskDetails: [
      {
        title: 'Preflight Checks',
        description: 'Perform preflight checks to determine if the task target is healthy.',
        state: TaskState.FAILURE,
        extraDetails: []
      }
    ],
    versionNumbers: {
      ybPrevSoftwareVersion: '2025.2.2.2-b11',
      ybSoftwareVersion: '2025.1.1.0-b110'
    }
  },
  abortable: false,
  retryable: false,
  canRollback: false,
  correlationId: 'a0000001-0001-4000-8000-000000000003',
  userEmail: 'admin',
  subtaskInfos: [],
  taskInfo: {
    taskParams: {}
  }
});

export const createDbUpgradePrecheckValidationTaskMock = (overrides: Partial<Task> = {}): Task => {
  const base = buildDbUpgradePrecheckValidationTaskFixture();
  return {
    ...base,
    ...overrides,
    details: {
      ...base.details,
      ...(overrides.details ?? {}),
      taskDetails: overrides.details?.taskDetails ?? base.details.taskDetails,
      versionNumbers: overrides.details?.versionNumbers ?? base.details.versionNumbers
    }
  };
};

/**
 * Mock body for `POST .../upgrade/software/precheck`
 */
export const createDbUpgradePrecheckMetadataMock = (
  overrides: Partial<UniverseSoftwareUpgradePrecheckResp> = {}
): UniverseSoftwareUpgradePrecheckResp => ({
  finalize_required: false,
  ysql_major_version_upgrade: false,
  ...overrides
});

export const DB_UPGRADE_ROLLBACK_TASK_ID = 'b0000001-0001-4000-8000-000000000001';

export const DB_UPGRADE_ROLLBACK_TASK_UNIVERSE_UUID =
  'b0000001-0001-4000-8000-000000000002';

const buildDbUpgradeRollbackTaskFixture = (): Task => ({
  id: DB_UPGRADE_ROLLBACK_TASK_ID,
  title: 'Rolling back upgrade : mock-universe',
  percentComplete: 30,
  createTime: '2026-04-21T10:00:00Z',
  completionTime: '2026-04-21T10:05:00Z',
  target: TargetType.UNIVERSE as Task['target'],
  targetUUID: DB_UPGRADE_ROLLBACK_TASK_UNIVERSE_UUID,
  type: TaskType.ROLLBACK_UPGRADE as Task['type'],
  typeName: 'Rollback Upgrade',
  status: TaskState.RUNNING,
  details: {
    taskDetails: [
      {
        title: 'Rolling back software',
        description: 'Rolling back YugaByte software on existing clusters.',
        state: TaskState.RUNNING,
        extraDetails: []
      }
    ],
    versionNumbers: {
      ybPrevSoftwareVersion: '2025.1.1.0-b110',
      ybSoftwareVersion: '2025.2.2.2-b11'
    }
  },
  abortable: true,
  retryable: true,
  canRollback: false,
  correlationId: 'b0000001-0001-4000-8000-000000000003',
  userEmail: 'admin',
  subtaskInfos: [],
  taskInfo: {
    taskParams: {}
  }
});

export const createDbUpgradeRollbackTaskMock = (overrides: Partial<Task> = {}): Task => {
  const base = buildDbUpgradeRollbackTaskFixture();
  return {
    ...base,
    ...overrides,
    details: {
      ...base.details,
      ...(overrides.details ?? {}),
      taskDetails: overrides.details?.taskDetails ?? base.details.taskDetails,
      versionNumbers: overrides.details?.versionNumbers ?? base.details.versionNumbers
    }
  };
};

export const DB_UPGRADE_FINALIZE_TASK_ID = 'c0000001-0001-4000-8000-000000000001';

export const DB_UPGRADE_FINALIZE_TASK_UNIVERSE_UUID =
  'c0000001-0001-4000-8000-000000000002';

const buildDbUpgradeFinalizeTaskFixture = (): Task => ({
  id: DB_UPGRADE_FINALIZE_TASK_ID,
  title: 'Finalizing upgrade : mock-universe',
  percentComplete: 90,
  createTime: '2026-04-21T10:00:00Z',
  completionTime: '2026-04-21T10:05:00Z',
  target: TargetType.UNIVERSE as Task['target'],
  targetUUID: DB_UPGRADE_FINALIZE_TASK_UNIVERSE_UUID,
  type: TaskType.FINALIZE_UPGRADE as Task['type'],
  typeName: 'Finalize Upgrade',
  status: TaskState.RUNNING,
  details: {
    taskDetails: [
      {
        title: 'Finalizing software',
        description: 'Finalizing YugaByte software upgrade on existing clusters.',
        state: TaskState.RUNNING,
        extraDetails: []
      }
    ],
    versionNumbers: {
      ybPrevSoftwareVersion: '2025.1.1.0-b110',
      ybSoftwareVersion: '2025.2.2.2-b11'
    }
  },
  abortable: true,
  retryable: true,
  canRollback: false,
  correlationId: 'c0000001-0001-4000-8000-000000000003',
  userEmail: 'admin',
  subtaskInfos: [],
  taskInfo: {
    taskParams: {}
  }
});

export const createDbUpgradeFinalizeTaskMock = (overrides: Partial<Task> = {}): Task => {
  const base = buildDbUpgradeFinalizeTaskFixture();
  return {
    ...base,
    ...overrides,
    details: {
      ...base.details,
      ...(overrides.details ?? {}),
      taskDetails: overrides.details?.taskDetails ?? base.details.taskDetails,
      versionNumbers: overrides.details?.versionNumbers ?? base.details.versionNumbers
    }
  };
};
