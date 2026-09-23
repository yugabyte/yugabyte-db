import { http, HttpResponse } from 'msw';

import { TaskState } from '@app/redesign/features/tasks/dtos';
import type { TaskStatus } from '@app/redesign/helpers/api';
import {
  EDIT_UNIVERSE_ROLLBACK_TASK_ID,
  EDIT_UNIVERSE_TASK_ID,
  EDIT_UNIVERSE_TASK_UNIVERSE_UUID
} from './taskMocks';
import { generateUniverseMockResponse } from './universeMocks';

export const EDIT_UNIVERSE_ROLLBACK_RETRY_TASK_ID = 'd0000001-0001-4000-8000-000000000007';

export type EditUniverseRollbackRetryOutcome = 'success' | 'failure';

const V1_CUSTOMER_ROOT = 'http://localhost:9000/api/v1/customers/customer-uuid';
const V2_CUSTOMER_ROOT = 'http://localhost:9000/api/v2/customers/customer-uuid';

/**
 * The banner reads `retryable` off the failed edit to tell a rollback precheck failure (placement
 * ownership still on the edit) from a failure after the universe was frozen.
 */
const buildFailedEditTaskStatus = (isRetryable: boolean): TaskStatus => ({
  title: 'Updated Universe : mock-universe',
  createTime: '2026-04-22T09:00:00Z',
  completionTime: '2026-04-22T09:03:00Z',
  target: 'mock-universe',
  targetUUID: EDIT_UNIVERSE_TASK_UNIVERSE_UUID,
  type: 'Update',
  status: TaskState.FAILURE,
  percent: 62,
  abortable: false,
  retryable: isRetryable,
  canRollback: false,
  userEmail: 'admin'
});

export const editUniverseRollbackTaskBannerMswHandlers = (
  getOutcome: () => EditUniverseRollbackRetryOutcome,
  getIsFailedEditRetryable: () => boolean
) => {
  const mockUniverse = generateUniverseMockResponse({
    universeUuid: EDIT_UNIVERSE_TASK_UNIVERSE_UUID
  });
  const respondToRetry = () => {
    if (getOutcome() === 'failure') {
      return HttpResponse.json({ error: 'Universe is locked' }, { status: 400 });
    }
    return HttpResponse.json({
      taskUUID: EDIT_UNIVERSE_ROLLBACK_RETRY_TASK_ID,
      resourceUUID: EDIT_UNIVERSE_TASK_UNIVERSE_UUID
    });
  };

  return [
    http.post(`${V1_CUSTOMER_ROOT}/tasks/${EDIT_UNIVERSE_ROLLBACK_TASK_ID}/retry`, respondToRetry),
    http.post(`${V1_CUSTOMER_ROOT}/tasks/${EDIT_UNIVERSE_TASK_ID}/retry`, respondToRetry),
    http.get(`${V1_CUSTOMER_ROOT}/tasks/${EDIT_UNIVERSE_ROLLBACK_RETRY_TASK_ID}`, () =>
      HttpResponse.json({ percent: 100, status: 'Success' })
    ),
    http.get(`${V1_CUSTOMER_ROOT}/tasks/${EDIT_UNIVERSE_TASK_ID}`, () =>
      HttpResponse.json(buildFailedEditTaskStatus(getIsFailedEditRetryable()))
    ),
    http.get(`${V1_CUSTOMER_ROOT}/tasks`, () => HttpResponse.json([])),
    http.get(`${V1_CUSTOMER_ROOT}/universes/${EDIT_UNIVERSE_TASK_UNIVERSE_UUID}`, () =>
      HttpResponse.json({ data: mockUniverse })
    ),
    http.get(`${V2_CUSTOMER_ROOT}/universes/${EDIT_UNIVERSE_TASK_UNIVERSE_UUID}`, () =>
      HttpResponse.json(mockUniverse)
    )
  ];
};
