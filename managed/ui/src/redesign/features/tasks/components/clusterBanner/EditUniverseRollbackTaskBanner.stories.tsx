import type { ComponentType } from 'react';

import type { Meta, StoryObj } from '@storybook/react-vite';
import { ToastContainer } from 'react-toastify';

import {
  editUniverseRollbackTaskBannerMswHandlers,
  type EditUniverseRollbackRetryOutcome
} from '@app/mocks/mock-data/editUniverseRollbackTaskBannerMswHandlers';
import {
  createEditUniverseRollbackTaskMock,
  EDIT_UNIVERSE_TASK_UNIVERSE_UUID
} from '@app/mocks/mock-data/taskMocks';
import { withStorybookTasksReduxProvider } from '@app/mocks/storybook/storybookTasksRedux';
import { TaskState, type Task } from '@app/redesign/features/tasks/dtos';
import { ToastNotificationDuration } from '@app/redesign/helpers/constants';

import { EditUniverseRollbackTaskBanner } from './EditUniverseRollbackTaskBanner';

import 'react-toastify/dist/ReactToastify.css';

let storyRetryOutcome: EditUniverseRollbackRetryOutcome = 'success';
let storyIsFailedEditRetryable = false;

type EditUniverseRollbackTaskBannerStoryArgs = {
  task: Task;
  universeUuid: string;
  /** Drives the mocked failed edit's `retryable`, which is how the banner detects a precheck failure. */
  isFailedEditRetryable: boolean;
  retryOutcome: EditUniverseRollbackRetryOutcome;
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
  title: 'Tasks/EditUniverseRollbackTaskBanner',
  component: EditUniverseRollbackTaskBanner,
  tags: ['autodocs'],
  decorators: [withCustomerId, withToastContainer, withStorybookTasksReduxProvider],
  args: {
    task: createEditUniverseRollbackTaskMock(),
    universeUuid: EDIT_UNIVERSE_TASK_UNIVERSE_UUID,
    isFailedEditRetryable: false,
    retryOutcome: 'success'
  },
  argTypes: {
    universeUuid: { control: false },
    isFailedEditRetryable: { control: false },
    retryOutcome: {
      name: 'Retry outcome',
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
    isFailedEditRetryable,
    retryOutcome
  }: EditUniverseRollbackTaskBannerStoryArgs) => {
    storyRetryOutcome = retryOutcome;
    storyIsFailedEditRetryable = isFailedEditRetryable;
    return <EditUniverseRollbackTaskBanner task={task} universeUuid={universeUuid} />;
  },
  parameters: {
    msw: {
      handlers: {
        editUniverseRollbackTaskBanner: editUniverseRollbackTaskBannerMswHandlers(
          () => storyRetryOutcome,
          () => storyIsFailedEditRetryable
        )
      }
    }
  }
} satisfies Meta<EditUniverseRollbackTaskBannerStoryArgs>;

export default meta;

type Story = StoryObj<typeof meta>;

export const InProgress: Story = {};

export const Failed: Story = {
  args: {
    task: createEditUniverseRollbackTaskMock({
      status: TaskState.FAILURE,
      retryable: true,
      percentComplete: 45
    })
  }
};

export const PrecheckFailed: Story = {
  args: {
    task: createEditUniverseRollbackTaskMock({
      status: TaskState.FAILURE,
      retryable: true,
      percentComplete: 0
    }),
    isFailedEditRetryable: true
  }
};

export const Aborted: Story = {
  args: {
    task: createEditUniverseRollbackTaskMock({
      status: TaskState.ABORTED,
      retryable: true
    })
  }
};

export const Succeeded: Story = {
  args: {
    task: createEditUniverseRollbackTaskMock({
      status: TaskState.SUCCESS,
      percentComplete: 100
    })
  }
};
