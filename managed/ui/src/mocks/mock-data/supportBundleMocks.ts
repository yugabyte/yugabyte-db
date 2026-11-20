import { delay, http, HttpResponse } from 'msw';

import {
  SupportBundle,
  SupportBundleComponentType,
  SupportBundlePagedResp,
  SupportBundleStatus
} from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';

/** Matches the v2 API whether stories call it through localhost:9000 or the same origin. */
const SUPPORT_BUNDLES_URL =
  '*/api/v2/customers/:customerUuid/universes/:universeUuid/support-bundles';

const DAY_IN_MS = 24 * 60 * 60 * 1000;

const RECOMMENDED_COMPONENTS = [
  SupportBundleComponentType.ApplicationLogs,
  SupportBundleComponentType.UniverseLogs,
  SupportBundleComponentType.GFlags,
  SupportBundleComponentType.Instance,
  SupportBundleComponentType.ErrorFiles,
  SupportBundleComponentType.CoreFiles,
  SupportBundleComponentType.TabletMeta
];

/**
 * A bundle collected over the day before `createdAt`. Only successful bundles have a creation
 * date, expiry and size, as with the real API.
 */
export const createSupportBundleMock = (
  uuid: string,
  status: SupportBundleStatus,
  {
    createdAt = new Date(),
    sizeInBytes = 4_100_000_000,
    components = RECOMMENDED_COMPONENTS,
    nodeNames
  }: {
    createdAt?: Date;
    sizeInBytes?: number;
    components?: SupportBundleComponentType[];
    nodeNames?: string[];
  } = {}
): SupportBundle => {
  const isSuccess = status === SupportBundleStatus.Success;
  return {
    info: {
      uuid,
      status,
      scope_uuid: 'universe-uuid',
      creation_date: isSuccess ? createdAt.toISOString() : undefined,
      expiration_date: isSuccess
        ? new Date(createdAt.getTime() + 10 * DAY_IN_MS).toISOString()
        : undefined,
      size_in_bytes: isSuccess ? sizeInBytes : 0
    },
    spec: {
      components,
      node_names: nodeNames,
      start_date: new Date(createdAt.getTime() - DAY_IN_MS).toISOString(),
      end_date: createdAt.toISOString()
    }
  };
};

/** One bundle in each status, newest first. */
export const createSupportBundleListMock = (now = new Date()): SupportBundle[] => [
  createSupportBundleMock('sb-running', SupportBundleStatus.Running, { createdAt: now }),
  createSupportBundleMock('sb-ready', SupportBundleStatus.Success, {
    createdAt: new Date(now.getTime() - 2 * 60 * 60 * 1000)
  }),
  createSupportBundleMock('sb-failed', SupportBundleStatus.Failed, {
    createdAt: new Date(now.getTime() - DAY_IN_MS)
  }),
  createSupportBundleMock('sb-old', SupportBundleStatus.Success, {
    createdAt: new Date(now.getTime() - 9 * DAY_IN_MS),
    sizeInBytes: 820_000_000
  }),
  createSupportBundleMock('sb-aborted', SupportBundleStatus.Aborted, {
    createdAt: new Date(now.getTime() - 9 * DAY_IN_MS)
  })
];

export type SupportBundleListMswOptions = {
  bundles?: SupportBundle[];
  /** Reported total; defaults to the number of bundles, set it higher to show paging. */
  totalCount?: number;
  /** `loading` never answers; `error` answers 500. */
  listState?: 'ok' | 'loading' | 'error';
};

/** Handlers for the support bundle list and delete calls. */
export const supportBundleListMswHandlers = ({
  bundles = [],
  totalCount = bundles.length,
  listState = 'ok'
}: SupportBundleListMswOptions = {}) => [
  http.post(`${SUPPORT_BUNDLES_URL}/page`, async ({ request }) => {
    if (listState === 'loading') {
      await delay('infinite');
    }
    if (listState === 'error') {
      return HttpResponse.json({ error: 'Internal error' }, { status: 500 });
    }
    const { offset = 0, limit = 25 } = (await request.json()) as {
      offset?: number;
      limit?: number;
    };
    const resp: SupportBundlePagedResp = {
      entities: bundles.slice(offset, offset + limit),
      total_count: totalCount,
      has_next: offset + limit < totalCount,
      has_prev: offset > 0
    };
    return HttpResponse.json(resp);
  }),
  http.delete(`${SUPPORT_BUNDLES_URL}/:bundleUuid`, () => new HttpResponse(null, { status: 204 }))
];
