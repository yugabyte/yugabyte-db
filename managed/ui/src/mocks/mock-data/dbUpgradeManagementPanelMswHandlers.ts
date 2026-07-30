import { http, HttpResponse } from 'msw';

import type { Task } from '@app/redesign/features/tasks/dtos';
import type {
  Universe,
  UniverseInfo,
  UniverseSoftwareUpgradePrecheckResp
} from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';

export type DbUpgradeManagementPanelMswOptions = {
  /** Merged into the GET universe mock `info` (e.g. `software_upgrade_state` per story). */
  universeInfoOverrides?: Partial<UniverseInfo>;
};

/**
 * MSW handlers for {@link DbUpgradeManagementSidePanel} API calls (universe task list, universe, precheck).
 * Reuse for stories that mount the panel or embed it (e.g. task banner).
 */
export const dbUpgradeManagementPanelMswHandlers = (
  universeUuid: string,
  task: Task,
  universe: Universe,
  precheckBody: UniverseSoftwareUpgradePrecheckResp,
  options?: DbUpgradeManagementPanelMswOptions
) => {
  const universeResponse: Universe =
    universe.info && options?.universeInfoOverrides
      ? {
          ...universe,
          info: { ...universe.info, ...options.universeInfoOverrides }
        }
      : universe;

  return [
    http.get('http://localhost:9000/api/v1/customers/customer-uuid/tasks_list', ({ request }) => {
      const url = new URL(request.url);
      if (url.searchParams.get('uUUID') !== universeUuid) {
        return HttpResponse.json([]);
      }
      return HttpResponse.json([task]);
    }),
    http.get(`http://localhost:9000/api/v2/customers/customer-uuid/universes/${universeUuid}`, () =>
      HttpResponse.json(universeResponse)
    ),
    http.post(
      `http://localhost:9000/api/v2/customers/customer-uuid/universes/${universeUuid}/upgrade/software/precheck`,
      () => HttpResponse.json(precheckBody)
    ),
    http.post(`http://localhost:9000/api/v1/customers/customer-uuid/tasks/${task.id}`, () =>
      HttpResponse.json({ taskUUID: task.id })
    )
  ];
};
