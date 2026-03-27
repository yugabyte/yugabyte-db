import {
  AZUpgradeState,
  AZUpgradeStatus,
  CanaryPauseState,
  CanaryUpgradeProgress,
  DbUpgradePrecheckStatus,
  ServerType,
  Task,
  TaskState,
  TaskType,
  TargetType
} from '@app/redesign/features/tasks/dtos';

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
 * Minimal canary progress for unit tests (valid {@link CanaryUpgradeProgress} without assertions).
 * Spread `partial` to override fields.
 */
export const createMinimalCanaryUpgradeProgressForTests = (
  partial: Partial<CanaryUpgradeProgress> = {}
): CanaryUpgradeProgress => ({
  enabled: true,
  pauseState: CanaryPauseState.NOT_PAUSED,
  precheckStatus: DbUpgradePrecheckStatus.RUNNING,
  masterAZUpgradeStatesList: [],
  tserverAZUpgradeStatesList: [],
  ...partial
});

export const defaultDbUpgradeCanaryProgress = (): CanaryUpgradeProgress => ({
  enabled: true,
  pauseState: CanaryPauseState.NOT_PAUSED,
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

type CreateDbUpgradeTaskMockOverrides = Partial<Omit<Task, 'canaryUpgradeProgress'>> & {
  canaryUpgradeProgress?: Partial<CanaryUpgradeProgress>;
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
    }
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
  },
  canaryUpgradeProgress: defaultDbUpgradeCanaryProgress()
});

export const createDbUpgradeTaskMock = (overrides: CreateDbUpgradeTaskMockOverrides = {}): Task => {
  const base = buildBaseDbUpgradeTask();
  const { canaryUpgradeProgress: canaryPartial, ...taskPartial } = overrides;
  return {
    ...base,
    ...taskPartial,
    canaryUpgradeProgress: {
      ...base.canaryUpgradeProgress!,
      ...canaryPartial
    }
  };
};

export const generateDbUpgradeTaskMockResponse = (): Task => createDbUpgradeTaskMock();
