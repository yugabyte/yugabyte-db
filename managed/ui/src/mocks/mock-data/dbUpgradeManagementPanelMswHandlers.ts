import { http, HttpResponse } from 'msw';

import type { Task } from '@app/redesign/features/tasks/dtos';
import {
  ReleaseState,
  ReleaseYbType,
  type YbdbRelease
} from '@app/redesign/features/universe/universe-actions/software-upgrade/dtos';
import type {
  Universe,
  UniverseInfo,
  UniverseSoftwareUpgradePrecheckResp
} from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';

export type DbUpgradeManagementPanelMswOptions = {
  /** Merged into the GET universe mock `info` (e.g. `software_upgrade_state` per story). */
  universeInfoOverrides?: Partial<UniverseInfo>;
};

const createActiveYbdbReleaseMock = (version: string): YbdbRelease => ({
  release_uuid: `release-uuid-${version}`,
  version,
  yb_type: ReleaseYbType.YBDB,
  release_type: 'LTS',
  state: ReleaseState.ACTIVE,
  artifacts: [
    { platform: 'LINUX', architecture: 'x86_64' },
    { platform: 'LINUX', architecture: 'aarch64' }
  ]
});

/**
 * Active YBDB releases for DB upgrade modal stories: current universe version plus any
 * versions on the task.
 */
export const buildDbUpgradeStoryReleaseMocks = (universe: Universe, task: Task): YbdbRelease[] => {
  const versions = new Set<string>();
  const currentVersion = universe.spec?.yb_software_version;
  if (currentVersion) {
    versions.add(currentVersion);
  }
  const targetVersion = task.details?.versionNumbers?.ybSoftwareVersion;
  if (targetVersion) {
    versions.add(targetVersion);
  }
  const previousVersion = task.details?.versionNumbers?.ybPrevSoftwareVersion;
  if (previousVersion) {
    versions.add(previousVersion);
  }
  return Array.from(versions).map(createActiveYbdbReleaseMock);
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

  const dbReleases = buildDbUpgradeStoryReleaseMocks(universeResponse, task);

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
    ),
    http.get('http://localhost:9000/api/v1/customers/customer-uuid/ybdb_release', () =>
      HttpResponse.json(dbReleases)
    ),
    http.get(
      `http://localhost:9000/api/v1/customers/customer-uuid/runtime_config/${universeUuid}`,
      () => HttpResponse.json({ configEntries: [] })
    )
  ];
};
