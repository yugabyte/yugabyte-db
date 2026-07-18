import {
  AZUpgradeStatus,
  CanaryPauseState,
  ServerType,
  Task,
  TaskState,
  TaskType,
  TargetType
} from '@app/redesign/features/tasks/dtos';

const TASK_UUID = 'a1b2c3d4-e5f6-7890-abcd-ef1234567890';
const UNIVERSE_UUID = 'b2c3d4e5-f6a7-8901-bcde-f12345678901';
const PRIMARY_CLUSTER_UUID = 'e5f6a7b8-c9d0-1234-ef01-345678901234';

/** SubTaskGroupType.PreflightChecks — not a CustomerTask type; asserted for SubTaskInfo typing. */
const PREFLIGHT_SUBTASK_GROUP = 'PreflightChecks' as Task['type'];

export const generateDbUpgradeTaskMockResponse = (): Task => {
  return {
    id: TASK_UUID,
    title: 'Upgrade Software : yb-demo-universe',
    percentComplete: 65,
    createTime: '2025-03-15T14:22:33Z',
    completionTime: '2025-03-15T19:45:12Z',
    target: TargetType.UNIVERSE as Task['target'],
    targetUUID: UNIVERSE_UUID,
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
    canaryUpgradeProgress: {
      enabled: true,
      pauseState: CanaryPauseState.PAUSED_AFTER_MASTERS,
      masterAZUpgradeStatesList: [
        {
          azUUID: 'us-west-2a-uuid',
          azName: 'us-west-2a',
          serverType: ServerType.MASTER,
          clusterUUID: PRIMARY_CLUSTER_UUID,
          status: AZUpgradeStatus.COMPLETED
        },
        {
          azUUID: 'us-west-2b-uuid',
          azName: 'us-west-2b',
          serverType: ServerType.MASTER,
          clusterUUID: PRIMARY_CLUSTER_UUID,
          status: AZUpgradeStatus.COMPLETED
        },
        {
          azUUID: 'sa-east-1a-uuid',
          azName: 'sa-east-1a',
          serverType: ServerType.MASTER,
          clusterUUID: PRIMARY_CLUSTER_UUID,
          status: AZUpgradeStatus.COMPLETED
        }
      ],
      tserverAZUpgradeStatesList: [
        {
          azUUID: 'us-west-2a-uuid',
          azName: 'us-west-2a',
          serverType: ServerType.TSERVER,
          clusterUUID: PRIMARY_CLUSTER_UUID,
          status: AZUpgradeStatus.NOT_STARTED
        },
        {
          azUUID: 'us-west-2b-uuid',
          azName: 'us-west-2b',
          serverType: ServerType.TSERVER,
          clusterUUID: PRIMARY_CLUSTER_UUID,
          status: AZUpgradeStatus.NOT_STARTED
        },
        {
          azUUID: 'sa-east-1a-uuid',
          azName: 'sa-east-1a',
          serverType: ServerType.TSERVER,
          clusterUUID: PRIMARY_CLUSTER_UUID,
          status: AZUpgradeStatus.NOT_STARTED
        }
      ]
    }
  };
};
