import { useMemo, type ComponentType } from 'react';

import type { Meta, StoryObj } from '@storybook/react-vite';
import { http, HttpResponse, delay } from 'msw';
import { Provider } from 'react-redux';

import {
  createEditUniverseRollbackTaskMock,
  createEditUniverseTaskMock,
  EDIT_UNIVERSE_ROLLBACK_TASK_ID,
  EDIT_UNIVERSE_TASK_ID,
  EDIT_UNIVERSE_TASK_UNIVERSE_UUID
} from '@app/mocks/mock-data/taskMocks';
import { createStorybookTasksRootStore } from '@app/mocks/storybook/storybookTasksRedux';
import { TaskState } from '@app/redesign/features/tasks/dtos';
import type { TaskStatus } from '@app/redesign/helpers/api';

import { TaskDetailDrawer } from '../TaskDetailDrawer';

const V1_CUSTOMER_ROOT = 'http://localhost:9000/api/v1/customers/customer-uuid';

const failedOriginalTaskStatus: TaskStatus = {
  title: 'Updated Universe : mock-universe',
  createTime: '2026-03-11T21:50:00Z',
  completionTime: '2026-03-11T22:00:00Z',
  target: 'mock-universe',
  targetUUID: EDIT_UNIVERSE_TASK_UNIVERSE_UUID,
  type: 'Update',
  status: TaskState.FAILURE,
  percent: 62,
  abortable: false,
  retryable: true,
  canRollback: false,
  userEmail: 'admin'
};

const rollbackTask = createEditUniverseRollbackTaskMock({
  status: TaskState.FAILURE,
  retryable: true,
  percentComplete: 10,
  completionTime: ''
});

const originalEditTask = createEditUniverseTaskMock({
  status: TaskState.FAILURE,
  percentComplete: 62
});

const withCustomerId = (Story: ComponentType) => {
  if (typeof window !== 'undefined') {
    window.localStorage.setItem('customerId', 'customer-uuid');
  }
  return <Story />;
};

const TaskOriginalTaskLinkStoryShell = ({ Story }: { Story: ComponentType }) => {
  const store = useMemo(
    () =>
      createStorybookTasksRootStore([rollbackTask, originalEditTask], {
        showTaskInDrawer: EDIT_UNIVERSE_ROLLBACK_TASK_ID
      }),
    []
  );

  return (
    <Provider store={store}>
      <Story />
      <TaskDetailDrawer />
    </Provider>
  );
};

const buildTaskDetailHandlers = (originalTaskDelayMs = 0) => [
  http.get(`${V1_CUSTOMER_ROOT}/tasks/${EDIT_UNIVERSE_TASK_ID}`, async () => {
    if (originalTaskDelayMs > 0) {
      await delay(originalTaskDelayMs);
    }
    return HttpResponse.json(failedOriginalTaskStatus);
  }),
  http.get(`${V1_CUSTOMER_ROOT}/tasks/${EDIT_UNIVERSE_ROLLBACK_TASK_ID}/details`, () =>
    HttpResponse.json({})
  ),
  http.get(`${V1_CUSTOMER_ROOT}/tasks/${EDIT_UNIVERSE_ROLLBACK_TASK_ID}/failed`, () =>
    HttpResponse.json({ failedSubTasks: [] })
  ),
  http.get(`${V1_CUSTOMER_ROOT}/tasks/${EDIT_UNIVERSE_TASK_ID}/details`, () =>
    HttpResponse.json({})
  ),
  http.get(`${V1_CUSTOMER_ROOT}/tasks/${EDIT_UNIVERSE_TASK_ID}/failed`, () =>
    HttpResponse.json({ failedSubTasks: [] })
  ),
  http.get(`${V1_CUSTOMER_ROOT}/tasks`, () => HttpResponse.json([]))
];

const meta = {
  title: 'Tasks/TaskOriginalTaskLink',
  component: TaskDetailDrawer,
  tags: ['autodocs'],
  decorators: [
    withCustomerId,
    (Story: ComponentType) => (
      <TaskOriginalTaskLinkStoryShell Story={Story} />
    )
  ],
  parameters: {
    msw: {
      handlers: {
        taskOriginalTaskLink: buildTaskDetailHandlers()
      }
    }
  }
} satisfies Meta;

export default meta;

type Story = StoryObj<typeof meta>;

export const WithOriginalTask: Story = {};

/** Keeps the original-task fetch pending so the Loading... + spinner state stays visible. */
export const LoadingOriginalTask: Story = {
  parameters: {
    msw: {
      handlers: {
        taskOriginalTaskLink: buildTaskDetailHandlers(60_000)
      }
    }
  }
};
