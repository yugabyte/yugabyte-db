import { useMemo, type ComponentType } from 'react';
import type { Meta, StoryObj } from '@storybook/react-vite';
import { Provider } from 'react-redux';

import { dbUpgradeManagementPanelMswHandlers } from '@app/mocks/mock-data/dbUpgradeManagementPanelMswHandlers';
import {
  createDbUpgradePrecheckMetadataMock,
  createDbUpgradePrecheckValidationTaskMock,
  createDbUpgradeTaskMock,
  DB_UPGRADE_PRECHECK_VALIDATION_TASK_UNIVERSE_UUID,
  DB_UPGRADE_TASK_MOCK_UNIVERSE_UUID
} from '@app/mocks/mock-data/taskMocks';
import { generateUniverseMockResponse } from '@app/mocks/mock-data/universeMocks';
import { createStorybookTasksRootStore } from '@app/mocks/storybook/storybookTasksRedux';
import { TaskState, type Task } from '@app/redesign/features/tasks/dtos';
import type { UniverseSoftwareUpgradePrecheckResp } from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';

import { TaskDetailDrawer } from '../TaskDetailDrawer';
import { DbUpgradePrecheckTaskBanner } from './DbUpgradePrecheckTaskBanner';

const mockUniverseForPrecheckValidation = generateUniverseMockResponse({
  universeUuid: DB_UPGRADE_PRECHECK_VALIDATION_TASK_UNIVERSE_UUID
});

const storyWithBannerMsw = (
  task: Task,
  universeUuid: string,
  precheckOverrides?: Partial<UniverseSoftwareUpgradePrecheckResp>
) => ({
  parameters: {
    msw: {
      handlers: {
        dbUpgradePrecheckTaskBanner: dbUpgradeManagementPanelMswHandlers(
          universeUuid,
          task,
          mockUniverseForPrecheckValidation,
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

const DbUpgradePrecheckTasksStoryShell = ({
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
  <DbUpgradePrecheckTasksStoryShell Story={Story} task={context.args?.task} />
);

const defaultDbUpgradePrecheckTask = createDbUpgradePrecheckValidationTaskMock({
  status: TaskState.RUNNING,
  percentComplete: 42,
  completionTime: '2026-04-20T21:50:00Z',
  details: {
    taskDetails: [
      {
        title: 'Preflight Checks',
        description: 'Perform preflight checks to determine if the task target is healthy.',
        state: TaskState.RUNNING,
        extraDetails: []
      }
    ],
    versionNumbers: {
      ybPrevSoftwareVersion: '2025.2.2.2-b11',
      ybSoftwareVersion: '2025.1.1.0-b110'
    }
  }
});

const meta = {
  title: 'Tasks/DbUpgradePrecheckTaskBanner',
  component: DbUpgradePrecheckTaskBanner,
  tags: ['autodocs'],
  parameters: storyWithBannerMsw(
    defaultDbUpgradePrecheckTask,
    DB_UPGRADE_PRECHECK_VALIDATION_TASK_UNIVERSE_UUID
  ).parameters,
  decorators: [withCustomerId, withTasksReduxAndDrawer, (Story: ComponentType) => <Story />],
  args: {
    universeUuid: DB_UPGRADE_PRECHECK_VALIDATION_TASK_UNIVERSE_UUID,
    task: defaultDbUpgradePrecheckTask
  }
} satisfies Meta<typeof DbUpgradePrecheckTaskBanner>;

export default meta;

type Story = StoryObj<typeof meta>;

const precheckRunningTask = createDbUpgradePrecheckValidationTaskMock({
  status: TaskState.RUNNING,
  percentComplete: 55,
  completionTime: '2026-04-20T21:52:00Z',
  details: {
    taskDetails: [
      {
        title: 'Preflight Checks',
        description: 'Perform preflight checks to determine if the task target is healthy.',
        state: TaskState.RUNNING,
        extraDetails: []
      }
    ],
    versionNumbers: {
      ybPrevSoftwareVersion: '2025.2.2.2-b11',
      ybSoftwareVersion: '2025.1.1.0-b110'
    }
  }
});

export const PrecheckRunning: Story = {
  ...storyWithBannerMsw(precheckRunningTask, DB_UPGRADE_PRECHECK_VALIDATION_TASK_UNIVERSE_UUID),
  args: {
    universeUuid: DB_UPGRADE_PRECHECK_VALIDATION_TASK_UNIVERSE_UUID,
    task: precheckRunningTask
  }
};

const precheckRunningYsqlTask = createDbUpgradePrecheckValidationTaskMock({
  status: TaskState.RUNNING,
  percentComplete: 55,
  completionTime: '2026-04-20T21:52:00Z',
  details: {
    taskDetails: [
      {
        title: 'Preflight Checks',
        description: 'Perform preflight checks to determine if the task target is healthy.',
        state: TaskState.RUNNING,
        extraDetails: []
      }
    ],
    versionNumbers: {
      ybPrevSoftwareVersion: '2025.2.2.2-b11',
      ybSoftwareVersion: '2025.1.1.0-b110'
    }
  }
});

export const PrecheckRunningYsqlMajorVersion: Story = {
  ...storyWithBannerMsw(
    precheckRunningYsqlTask,
    DB_UPGRADE_PRECHECK_VALIDATION_TASK_UNIVERSE_UUID,
    {
      ysql_major_version_upgrade: true
    }
  ),
  args: {
    universeUuid: DB_UPGRADE_PRECHECK_VALIDATION_TASK_UNIVERSE_UUID,
    task: precheckRunningYsqlTask
  }
};

const precheckPassedTask = createDbUpgradePrecheckValidationTaskMock({
  status: TaskState.SUCCESS,
  percentComplete: 100,
  completionTime: '2026-04-20T22:00:00Z',
  details: {
    taskDetails: [
      {
        title: 'Preflight Checks',
        description: 'Perform preflight checks to determine if the task target is healthy.',
        state: TaskState.SUCCESS,
        extraDetails: []
      }
    ],
    versionNumbers: {
      ybPrevSoftwareVersion: '2025.2.2.2-b11',
      ybSoftwareVersion: '2025.1.1.0-b110'
    }
  }
});

export const PrecheckPassed: Story = {
  ...storyWithBannerMsw(precheckPassedTask, DB_UPGRADE_PRECHECK_VALIDATION_TASK_UNIVERSE_UUID),
  args: {
    universeUuid: DB_UPGRADE_PRECHECK_VALIDATION_TASK_UNIVERSE_UUID,
    task: precheckPassedTask
  }
};

const precheckPassedYsqlTask = createDbUpgradePrecheckValidationTaskMock({
  status: TaskState.SUCCESS,
  percentComplete: 100,
  completionTime: '2026-04-20T22:00:00Z',
  details: {
    taskDetails: [
      {
        title: 'Preflight Checks',
        description: 'Perform preflight checks to determine if the task target is healthy.',
        state: TaskState.SUCCESS,
        extraDetails: []
      }
    ],
    versionNumbers: {
      ybPrevSoftwareVersion: '2025.2.2.2-b11',
      ybSoftwareVersion: '2025.1.1.0-b110'
    }
  }
});

export const PrecheckPassedYsqlMajorVersion: Story = {
  ...storyWithBannerMsw(precheckPassedYsqlTask, DB_UPGRADE_PRECHECK_VALIDATION_TASK_UNIVERSE_UUID, {
    ysql_major_version_upgrade: true
  }),
  args: {
    universeUuid: DB_UPGRADE_PRECHECK_VALIDATION_TASK_UNIVERSE_UUID,
    task: precheckPassedYsqlTask
  }
};

/** Failed validation precheck — default fixture matches the captured API payload. */
export const PrecheckFailed: Story = {
  ...storyWithBannerMsw(
    createDbUpgradePrecheckValidationTaskMock(),
    DB_UPGRADE_PRECHECK_VALIDATION_TASK_UNIVERSE_UUID
  ),
  args: {
    universeUuid: DB_UPGRADE_PRECHECK_VALIDATION_TASK_UNIVERSE_UUID,
    task: createDbUpgradePrecheckValidationTaskMock()
  }
};

const precheckFailedYsqlTask = createDbUpgradePrecheckValidationTaskMock({
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
  }
});

export const PrecheckFailedYsqlMajorVersion: Story = {
  ...storyWithBannerMsw(precheckFailedYsqlTask, DB_UPGRADE_PRECHECK_VALIDATION_TASK_UNIVERSE_UUID, {
    ysql_major_version_upgrade: true
  }),
  args: {
    universeUuid: DB_UPGRADE_PRECHECK_VALIDATION_TASK_UNIVERSE_UUID,
    task: precheckFailedYsqlTask
  }
};

export const NonPrecheckTask: Story = {
  args: {
    universeUuid: DB_UPGRADE_TASK_MOCK_UNIVERSE_UUID,
    task: createDbUpgradeTaskMock({ typeName: 'Software Upgrade' })
  }
};
