import { http, HttpResponse } from 'msw';

import {
  EDIT_UNIVERSE_TASK_ID,
  EDIT_UNIVERSE_TASK_UNIVERSE_UUID
} from './taskMocks';
import { generateUniverseMockResponse } from './universeMocks';

/** New commissioner task UUID returned by retry / rollback (not the failed fixture id). */
export const EDIT_UNIVERSE_SUBMITTED_TASK_ID = 'd0000001-0001-4000-8000-000000000004';

export type EditUniverseTaskActionOutcome = 'success' | 'failure';

const V1_CUSTOMER_ROOT = 'http://localhost:9000/api/v1/customers/customer-uuid';
const V2_CUSTOMER_ROOT = 'http://localhost:9000/api/v2/customers/customer-uuid';

/**
 * MSW handlers for {@link EditUniverseTaskBanner} retry / rollback.
 *
 * `getOutcome` is read on each request so a Storybook control can switch success vs request
 * failure without remounting the worker.
 */
export const editUniverseTaskBannerMswHandlers = (
  getOutcome: () => EditUniverseTaskActionOutcome
) => {
  const mockUniverse = generateUniverseMockResponse({
    universeUuid: EDIT_UNIVERSE_TASK_UNIVERSE_UUID
  });

  const respondToRetry = () => {
    if (getOutcome() === 'failure') {
      return HttpResponse.json({ error: 'Universe is locked' }, { status: 400 });
    }
    return HttpResponse.json({
      task_uuid: EDIT_UNIVERSE_SUBMITTED_TASK_ID,
      resource_uuid: EDIT_UNIVERSE_TASK_UNIVERSE_UUID
    });
  };

  const respondToRollback = () => {
    if (getOutcome() === 'failure') {
      return HttpResponse.json({ error: 'Universe is locked' }, { status: 400 });
    }
    return HttpResponse.json({
      task_uuid: EDIT_UNIVERSE_SUBMITTED_TASK_ID,
      resource_uuid: EDIT_UNIVERSE_TASK_UNIVERSE_UUID
    });
  };

  return [
    http.post(`${V2_CUSTOMER_ROOT}/tasks/${EDIT_UNIVERSE_TASK_ID}/retry`, respondToRetry),
    http.post(`${V2_CUSTOMER_ROOT}/tasks/${EDIT_UNIVERSE_TASK_ID}/rollback`, respondToRollback),
    http.get(`${V1_CUSTOMER_ROOT}/tasks/${EDIT_UNIVERSE_SUBMITTED_TASK_ID}`, () =>
      HttpResponse.json({ percent: 100, status: 'Success' })
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
