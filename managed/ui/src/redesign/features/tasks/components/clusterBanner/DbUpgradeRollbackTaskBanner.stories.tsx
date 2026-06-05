import { useMemo, type ComponentType } from 'react';
import type { Meta, StoryObj } from '@storybook/react-vite';
import { Provider } from 'react-redux';

import { dbUpgradeManagementPanelMswHandlers } from '@app/mocks/mock-data/dbUpgradeManagementPanelMswHandlers';
import {
  createDbUpgradePrecheckMetadataMock,
  createDbUpgradeRollbackTaskMock,
  createDbUpgradeTaskMock,
  DB_UPGRADE_ROLLBACK_TASK_UNIVERSE_UUID,
  DB_UPGRADE_TASK_MOCK_UNIVERSE_UUID
} from '@app/mocks/mock-data/taskMocks';
import { generateUniverseMockResponse } from '@app/mocks/mock-data/universeMocks';
import { createStorybookTasksRootStore } from '@app/mocks/storybook/storybookTasksRedux';
import { TaskState, type Task } from '@app/redesign/features/tasks/dtos';
import type { UniverseSoftwareUpgradePrecheckResp } from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';

import { TaskDetailDrawer } from '../TaskDetailDrawer';
import { DbUpgradeRollbackTaskBanner } from './DbUpgradeRollbackTaskBanner';

const mockUniverseForRollback = generateUniverseMockResponse({
  universeUuid: DB_UPGRADE_ROLLBACK_TASK_UNIVERSE_UUID
});

const storyWithBannerMsw = (
  task: Task,
  universeUuid: string,
  precheckOverrides?: Partial<UniverseSoftwareUpgradePrecheckResp>
) => ({
  parameters: {
    msw: {
      handlers: {
        dbUpgradeRollbackTaskBanner: dbUpgradeManagementPanelMswHandlers(
          universeUuid,
          task,
          mockUniverseForRollback,
          createDbUpgradePrecheckMetadataMock(precheckOverrides ?? {})
        )
      }
    }
  }
});

const withCustomerId = (Story: ComponentType) => {
  if (typeof window !== 'undefined') {
    window.localStorage.setItem('customerId', 'customer-uuid');
  }
  return <Story />;
};

const taskStoreSeedKey = (task: Task | undefined): string => {
  if (!task) {
    return '';
  }
  return `${task.id}-${task.status}-${task.percentComplete}-${task.completionTime ?? ''}`;
};

const DbUpgradeRollbackTasksStoryShell = ({
  Story,
  task
}: {
  Story: ComponentType;
  task?: Task;
}) => {
  const taskSeedKey = taskStoreSeedKey(task);
  const store = useMemo(
    () => createStorybookTasksRootStore(task ? [task] : undefined),
    [taskSeedKey]
  );

  return (
    <Provider store={store}>
      <Story />
      <TaskDetailDrawer />
    </Provider>
  );
};

const withTasksReduxAndDrawer = (Story: ComponentType, context: { args: { task?: Task } }) => (
  <DbUpgradeRollbackTasksStoryShell Story={Story} task={context.args?.task} />
);

const defaultDbUpgradeRollbackTask = createDbUpgradeRollbackTaskMock({
  status: TaskState.RUNNING,
  percentComplete: 30,
  completionTime: '2026-04-21T10:05:00Z'
});

const meta = {
  title: 'Tasks/DbUpgradeRollbackTaskBanner',
  component: DbUpgradeRollbackTaskBanner,
  tags: ['autodocs'],
  parameters: storyWithBannerMsw(
    defaultDbUpgradeRollbackTask,
    DB_UPGRADE_ROLLBACK_TASK_UNIVERSE_UUID
  ).parameters,
  decorators: [withCustomerId, withTasksReduxAndDrawer, (Story: ComponentType) => <Story />],
  args: {
    universeUuid: DB_UPGRADE_ROLLBACK_TASK_UNIVERSE_UUID,
    task: defaultDbUpgradeRollbackTask
  }
} satisfies Meta<typeof DbUpgradeRollbackTaskBanner>;

export default meta;

type Story = StoryObj<typeof meta>;

const rollbackRunningTask = createDbUpgradeRollbackTaskMock({
  status: TaskState.RUNNING,
  percentComplete: 30,
  completionTime: '2026-04-21T10:05:00Z'
});

export const RollbackRunning: Story = {
  ...storyWithBannerMsw(rollbackRunningTask, DB_UPGRADE_ROLLBACK_TASK_UNIVERSE_UUID),
  args: {
    universeUuid: DB_UPGRADE_ROLLBACK_TASK_UNIVERSE_UUID,
    task: rollbackRunningTask
  }
};

const rollbackRunningYsqlTask = createDbUpgradeRollbackTaskMock({
  status: TaskState.RUNNING,
  percentComplete: 30,
  completionTime: '2026-04-21T10:05:00Z'
});

export const RollbackRunningYsqlMajorVersion: Story = {
  ...storyWithBannerMsw(
    rollbackRunningYsqlTask,
    DB_UPGRADE_ROLLBACK_TASK_UNIVERSE_UUID,
    {
      ysql_major_version_upgrade: true
    }
  ),
  args: {
    universeUuid: DB_UPGRADE_ROLLBACK_TASK_UNIVERSE_UUID,
    task: rollbackRunningYsqlTask
  }
};

const rollbackFailedTask = createDbUpgradeRollbackTaskMock({
  status: TaskState.FAILURE,
  percentComplete: 10,
  completionTime: '2026-04-21T10:06:00Z',
  retryable: true,
  details: {
    taskDetails: [
      {
        title: 'Rolling back software',
        description: 'Rolling back YugaByte software on existing clusters.',
        state: TaskState.FAILURE,
        extraDetails: []
      }
    ],
    versionNumbers: {
      ybPrevSoftwareVersion: '2025.1.1.0-b110',
      ybSoftwareVersion: '2025.2.2.2-b11'
    }
  }
});

export const RollbackFailed: Story = {
  ...storyWithBannerMsw(rollbackFailedTask, DB_UPGRADE_ROLLBACK_TASK_UNIVERSE_UUID),
  args: {
    universeUuid: DB_UPGRADE_ROLLBACK_TASK_UNIVERSE_UUID,
    task: rollbackFailedTask
  }
};

const rollbackFailedNonRetryableTask = createDbUpgradeRollbackTaskMock({
  status: TaskState.FAILURE,
  percentComplete: 10,
  completionTime: '2026-04-21T10:06:00Z',
  retryable: false,
  details: {
    taskDetails: [
      {
        title: 'Rolling back software',
        description: 'Rolling back YugaByte software on existing clusters.',
        state: TaskState.FAILURE,
        extraDetails: []
      }
    ],
    versionNumbers: {
      ybPrevSoftwareVersion: '2025.1.1.0-b110',
      ybSoftwareVersion: '2025.2.2.2-b11'
    }
  }
});

export const RollbackFailedNonRetryable: Story = {
  ...storyWithBannerMsw(
    rollbackFailedNonRetryableTask,
    DB_UPGRADE_ROLLBACK_TASK_UNIVERSE_UUID
  ),
  args: {
    universeUuid: DB_UPGRADE_ROLLBACK_TASK_UNIVERSE_UUID,
    task: rollbackFailedNonRetryableTask
  }
};

export const NonRollbackTask: Story = {
  args: {
    universeUuid: DB_UPGRADE_TASK_MOCK_UNIVERSE_UUID,
    task: createDbUpgradeTaskMock({ typeName: 'Software Upgrade' })
  }
};
