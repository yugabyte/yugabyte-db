import type { ComponentType } from 'react';
import type { Meta, StoryObj } from '@storybook/react-vite';
import { ToastContainer } from 'react-toastify';

import {
  editUniverseTaskBannerMswHandlers,
  type EditUniverseTaskActionOutcome
} from '@app/mocks/mock-data/editUniverseTaskBannerMswHandlers';
import {
  createEditUniverseTaskMock,
  EDIT_UNIVERSE_TASK_UNIVERSE_UUID
} from '@app/mocks/mock-data/taskMocks';
import { withStorybookTasksReduxProvider } from '@app/mocks/storybook/storybookTasksRedux';
import { TargetType, TaskState, type Task } from '@app/redesign/features/tasks/dtos';
import { ToastNotificationDuration } from '@app/redesign/helpers/constants';

import { EditUniverseTaskBanner } from './EditUniverseTaskBanner';

import 'react-toastify/dist/ReactToastify.css';

let storyTaskActionOutcome: EditUniverseTaskActionOutcome = 'success';

type EditUniverseTaskBannerStoryArgs = {
  task: Task;
  universeUuid: string;
  onDismiss: () => void;
  taskActionOutcome: EditUniverseTaskActionOutcome;
};

const withCustomerId = (Story: ComponentType) => {
  if (typeof window !== 'undefined') {
    window.localStorage.setItem('customerId', 'customer-uuid');
  }
  return <Story />;
};

const withToastContainer = (Story: ComponentType) => (
  <>
    <Story />
    <ToastContainer
      hideProgressBar
      position="top-center"
      autoClose={ToastNotificationDuration.DEFAULT}
    />
  </>
);

const meta = {
  title: 'Tasks/EditUniverseTaskBanner',
  component: EditUniverseTaskBanner,
  tags: ['autodocs'],
  decorators: [withCustomerId, withToastContainer, withStorybookTasksReduxProvider],
  args: {
    task: createEditUniverseTaskMock(),
    universeUuid: EDIT_UNIVERSE_TASK_UNIVERSE_UUID,
    taskActionOutcome: 'success'
  },
  argTypes: {
    universeUuid: { control: false },
    onDismiss: { action: 'onDismiss' },
    taskActionOutcome: {
      name: 'Retry / rollback outcome',
      options: ['success', 'failure'],
      control: {
        type: 'select',
        labels: {
          success: 'Task succeeds',
          failure: 'Request fails'
        }
      }
    },
    task: { control: false }
  },
  render: ({
    task,
    universeUuid,
    onDismiss,
    taskActionOutcome
  }: EditUniverseTaskBannerStoryArgs) => {
    storyTaskActionOutcome = taskActionOutcome;
    return (
      <EditUniverseTaskBanner task={task} universeUuid={universeUuid} onDismiss={onDismiss} />
    );
  },
  parameters: {
    msw: {
      handlers: {
        editUniverseTaskBanner: editUniverseTaskBannerMswHandlers(() => storyTaskActionOutcome)
      }
    }
  }
} satisfies Meta<EditUniverseTaskBannerStoryArgs>;

export default meta;

type Story = StoryObj<typeof meta>;

export const InProgress: Story = {
  args: {
    task: createEditUniverseTaskMock({
      status: TaskState.RUNNING,
      percentComplete: 5,
      completionTime: ''
    })
  }
};

/** Terminal success. Dismissible — the only state where the banner can be acknowledged away. */
export const Succeeded: Story = {
  args: {
    task: createEditUniverseTaskMock({
      status: TaskState.SUCCESS,
      percentComplete: 100
    })
  }
};

export const FailedWithRetryAndRollback: Story = {
  args: {
    task: createEditUniverseTaskMock({ retryable: true, canRollback: true })
  }
};

export const FailedWithoutRollback: Story = {
  args: {
    task: createEditUniverseTaskMock({ retryable: true, canRollback: false })
  }
};

export const FailedWithoutRetry: Story = {
  args: {
    task: createEditUniverseTaskMock({ retryable: false, canRollback: true })
  }
};

/** Read replica edits are recorded against a `Cluster` target and use the same banner. */
export const FailedReadReplicaEdit: Story = {
  args: {
    task: createEditUniverseTaskMock({
      target: TargetType.CLUSTER as Task['target'],
      title: 'Updated Cluster : mock-universe'
    })
  }
};

/** User aborted the edit mid-flight — treated the same as a failure. */
export const Aborted: Story = {
  args: {
    task: createEditUniverseTaskMock({ status: TaskState.ABORTED })
  }
};
