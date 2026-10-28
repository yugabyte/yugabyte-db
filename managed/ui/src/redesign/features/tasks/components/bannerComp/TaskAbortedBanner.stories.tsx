import type { ComponentType } from 'react';
import type { Meta, StoryObj } from '@storybook/react-vite';
import { ToastContainer } from 'react-toastify';

import {
  pauseUniverseTaskBannerMswHandlers,
  type EditUniverseTaskActionOutcome
} from '@app/mocks/mock-data/editUniverseTaskBannerMswHandlers';
import { createPauseUniverseTaskMock } from '@app/mocks/mock-data/taskMocks';
import { withStorybookTasksReduxProvider } from '@app/mocks/storybook/storybookTasksRedux';
import { type Task } from '@app/redesign/features/tasks/dtos';
import { ToastNotificationDuration } from '@app/redesign/helpers/constants';

import { TaskAbortedBanner } from './TaskAbortedBanner';

import 'react-toastify/dist/ReactToastify.css';

let storyTaskActionOutcome: EditUniverseTaskActionOutcome = 'success';

type TaskAbortedBannerStoryArgs = {
  currentTask: Task;
  viewDetails: () => void;
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

const abortedTask = (overrides: Partial<Task> = {}): Task => createPauseUniverseTaskMock(overrides);

const meta = {
  title: 'Tasks/TaskAbortedBanner',
  component: TaskAbortedBanner,
  tags: ['autodocs'],
  decorators: [withCustomerId, withToastContainer, withStorybookTasksReduxProvider],
  args: {
    currentTask: abortedTask(),
    taskActionOutcome: 'success'
  },
  argTypes: {
    viewDetails: { action: 'viewDetails' },
    taskActionOutcome: {
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
    currentTask: { control: false }
  },
  render: ({ currentTask, viewDetails, taskActionOutcome }: TaskAbortedBannerStoryArgs) => {
    storyTaskActionOutcome = taskActionOutcome;
    return <TaskAbortedBanner currentTask={currentTask} viewDetails={viewDetails} />;
  },
  parameters: {
    msw: {
      handlers: {
        taskAbortedBanner: pauseUniverseTaskBannerMswHandlers(() => storyTaskActionOutcome)
      }
    }
  }
} satisfies Meta<TaskAbortedBannerStoryArgs>;

export default meta;

type Story = StoryObj<typeof meta>;

export const WithRetry: Story = {
  args: {
    currentTask: abortedTask({ retryable: true })
  }
};

export const WithoutRetry: Story = {
  args: {
    currentTask: abortedTask({ retryable: false })
  }
};
