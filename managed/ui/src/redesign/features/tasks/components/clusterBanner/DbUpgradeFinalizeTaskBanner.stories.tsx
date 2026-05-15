import { useMemo, type ComponentType } from 'react';
import type { Meta, StoryObj } from '@storybook/react-vite';
import { Provider } from 'react-redux';

import { dbUpgradeManagementPanelMswHandlers } from '@app/mocks/mock-data/dbUpgradeManagementPanelMswHandlers';
import {
  createDbUpgradeFinalizeTaskMock,
  createDbUpgradePrecheckMetadataMock,
  createDbUpgradeTaskMock,
  DB_UPGRADE_FINALIZE_TASK_UNIVERSE_UUID,
  DB_UPGRADE_TASK_MOCK_UNIVERSE_UUID
} from '@app/mocks/mock-data/taskMocks';
import { generateUniverseMockResponse } from '@app/mocks/mock-data/universeMocks';
import { createStorybookTasksRootStore } from '@app/mocks/storybook/storybookTasksRedux';
import { TaskState, type Task } from '@app/redesign/features/tasks/dtos';
import type { UniverseSoftwareUpgradePrecheckResp } from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';

import { TaskDetailDrawer } from '../TaskDetailDrawer';
import { DbUpgradeFinalizeTaskBanner } from './DbUpgradeFinalizeTaskBanner';

const mockUniverseForFinalize = generateUniverseMockResponse({
  universeUuid: DB_UPGRADE_FINALIZE_TASK_UNIVERSE_UUID
});

const storyWithBannerMsw = (
  task: Task,
  universeUuid: string,
  precheckOverrides?: Partial<UniverseSoftwareUpgradePrecheckResp>
) => ({
  parameters: {
    msw: {
      handlers: {
        dbUpgradeFinalizeTaskBanner: dbUpgradeManagementPanelMswHandlers(
          universeUuid,
          task,
          mockUniverseForFinalize,
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

const DbUpgradeFinalizeTasksStoryShell = ({
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
  <DbUpgradeFinalizeTasksStoryShell Story={Story} task={context.args?.task} />
);

const defaultDbUpgradeFinalizeTask = createDbUpgradeFinalizeTaskMock({
  status: TaskState.RUNNING,
  percentComplete: 90,
  completionTime: '2026-04-21T10:05:00Z'
});

const meta = {
  title: 'Tasks/DbUpgradeFinalizeTaskBanner',
  component: DbUpgradeFinalizeTaskBanner,
  tags: ['autodocs'],
  parameters: storyWithBannerMsw(
    defaultDbUpgradeFinalizeTask,
    DB_UPGRADE_FINALIZE_TASK_UNIVERSE_UUID
  ).parameters,
  decorators: [withCustomerId, withTasksReduxAndDrawer, (Story: ComponentType) => <Story />],
  args: {
    universeUuid: DB_UPGRADE_FINALIZE_TASK_UNIVERSE_UUID,
    task: defaultDbUpgradeFinalizeTask
  }
} satisfies Meta<typeof DbUpgradeFinalizeTaskBanner>;

export default meta;

type Story = StoryObj<typeof meta>;

const finalizeRunningTask = createDbUpgradeFinalizeTaskMock({
  status: TaskState.RUNNING,
  percentComplete: 90,
  completionTime: '2026-04-21T10:05:00Z'
});

export const FinalizeRunning: Story = {
  ...storyWithBannerMsw(finalizeRunningTask, DB_UPGRADE_FINALIZE_TASK_UNIVERSE_UUID),
  args: {
    universeUuid: DB_UPGRADE_FINALIZE_TASK_UNIVERSE_UUID,
    task: finalizeRunningTask
  }
};

const finalizeRunningYsqlTask = createDbUpgradeFinalizeTaskMock({
  status: TaskState.RUNNING,
  percentComplete: 90,
  completionTime: '2026-04-21T10:05:00Z'
});

export const FinalizeRunningYsqlMajorVersion: Story = {
  ...storyWithBannerMsw(
    finalizeRunningYsqlTask,
    DB_UPGRADE_FINALIZE_TASK_UNIVERSE_UUID,
    {
      ysql_major_version_upgrade: true
    }
  ),
  args: {
    universeUuid: DB_UPGRADE_FINALIZE_TASK_UNIVERSE_UUID,
    task: finalizeRunningYsqlTask
  }
};

const finalizeFailedTask = createDbUpgradeFinalizeTaskMock({
  status: TaskState.FAILURE,
  percentComplete: 50,
  completionTime: '2026-04-21T10:06:00Z',
  retryable: true,
  details: {
    taskDetails: [
      {
        title: 'Finalizing software',
        description: 'Finalizing YugaByte software upgrade on existing clusters.',
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

export const FinalizeFailed: Story = {
  ...storyWithBannerMsw(finalizeFailedTask, DB_UPGRADE_FINALIZE_TASK_UNIVERSE_UUID),
  args: {
    universeUuid: DB_UPGRADE_FINALIZE_TASK_UNIVERSE_UUID,
    task: finalizeFailedTask
  }
};

const finalizeFailedNonRetryableTask = createDbUpgradeFinalizeTaskMock({
  status: TaskState.FAILURE,
  percentComplete: 50,
  completionTime: '2026-04-21T10:06:00Z',
  retryable: false,
  details: {
    taskDetails: [
      {
        title: 'Finalizing software',
        description: 'Finalizing YugaByte software upgrade on existing clusters.',
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

export const FinalizeFailedNonRetryable: Story = {
  ...storyWithBannerMsw(
    finalizeFailedNonRetryableTask,
    DB_UPGRADE_FINALIZE_TASK_UNIVERSE_UUID
  ),
  args: {
    universeUuid: DB_UPGRADE_FINALIZE_TASK_UNIVERSE_UUID,
    task: finalizeFailedNonRetryableTask
  }
};

export const NonFinalizeTask: Story = {
  args: {
    universeUuid: DB_UPGRADE_TASK_MOCK_UNIVERSE_UUID,
    task: createDbUpgradeTaskMock({ typeName: 'Software Upgrade' })
  }
};
