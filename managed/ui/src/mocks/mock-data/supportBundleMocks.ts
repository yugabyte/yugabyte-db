import { delay, http, HttpResponse } from 'msw';

import type { Universe } from '@app/redesign/features/universe/universe-form/utils/dto';
import {
  SupportBundle,
  SupportBundleComponentType,
  SupportBundleCreateSpec,
  SupportBundlePagedResp,
  SupportBundleSizeEstimateResponse,
  SupportBundleStatus,
  YBATask
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
 * A bundle collected over the day before `createdAt`. As with the real API, every bundle has a
 * creation date, and only successful ones have an expiry and size.
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
      creation_date: createdAt.toISOString(),
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

/**
 * A universe as the legacy universe API returns it, trimmed to what the support bundle UI reads:
 * nodes with their regions, and the provider type of the primary cluster.
 */
export const createSupportBundleUniverseMock = ({
  name = 'puppy-food',
  regions = ['us-east-1', 'us-west-2', 'eu-west-1'],
  nodesPerRegion = 3,
  isKubernetes = false
}: {
  name?: string;
  regions?: string[];
  nodesPerRegion?: number;
  isKubernetes?: boolean;
} = {}): Universe => {
  let nodeIndex = 0;
  const nodeDetailsSet = regions.flatMap((region) =>
    Array.from({ length: nodesPerRegion }, () => {
      nodeIndex += 1;
      return { nodeName: `yb-dev-${name}-n${nodeIndex}`, cloudInfo: { region } };
    })
  );
  return {
    universeUUID: 'universe-uuid',
    name,
    universeDetails: {
      universeUUID: 'universe-uuid',
      updateInProgress: false,
      nodeDetailsSet,
      clusters: [
        {
          clusterType: 'PRIMARY',
          userIntent: { providerType: isKubernetes ? 'kubernetes' : 'aws' }
        }
      ]
    }
  } as unknown as Universe;
};

/** Collected once from the YBA host rather than from each node. */
const YBA_COMPONENTS: string[] = [
  SupportBundleComponentType.ApplicationLogs,
  SupportBundleComponentType.SystemLogs,
  SupportBundleComponentType.YbaMetadata
];
const COMPONENT_BYTES: Partial<Record<SupportBundleComponentType, number>> = {
  [SupportBundleComponentType.ApplicationLogs]: 1_300_000_000,
  [SupportBundleComponentType.SystemLogs]: 240_000_000,
  [SupportBundleComponentType.YbaMetadata]: 12_000_000,
  [SupportBundleComponentType.UniverseLogs]: 420_000_000,
  [SupportBundleComponentType.CoreFiles]: 150_000_000,
  [SupportBundleComponentType.PrometheusMetrics]: 210_000_000,
  [SupportBundleComponentType.PerfAdvisor]: 35_000_000
};
const DEFAULT_COMPONENT_BYTES = 4_000_000;

/** Sizes each requested component on each requested node, like the estimate-size API. */
export const estimateSupportBundleMock = (
  spec: SupportBundleCreateSpec,
  universe: Universe,
  scale = 1
): SupportBundleSizeEstimateResponse => {
  const nodeNames =
    spec.node_names ??
    universe.universeDetails.nodeDetailsSet.map((node) => node.nodeName as string);
  const bytes = (component: string) =>
    Math.round(
      (COMPONENT_BYTES[component as SupportBundleComponentType] ?? DEFAULT_COMPONENT_BYTES) * scale
    );
  const ybaComponents = spec.components.filter((component) => YBA_COMPONENTS.includes(component));
  const nodeComponents = spec.components.filter((component) => !YBA_COMPONENTS.includes(component));
  const data: Record<string, Record<string, number>> = {};
  if (ybaComponents.length > 0) {
    data.YBA = Object.fromEntries(ybaComponents.map((component) => [component, bytes(component)]));
  }
  nodeNames.forEach((nodeName) => {
    data[nodeName] = Object.fromEntries(
      nodeComponents.map((component) => [component, bytes(component)])
    );
  });
  return { data };
};

export type SupportBundleCreateMswOptions = {
  universe: Universe;
  /** Multiplies every component size, e.g. to push the estimate over the 20 GB warning. */
  estimateScale?: number;
  /** `loading` never answers the estimate. */
  estimateState?: 'ok' | 'loading';
  isPerfAdvisorRegistered?: boolean;
};

/** Handlers for the create page: size estimate, create, and the Perf Advisor registration. */
export const supportBundleCreateMswHandlers = ({
  universe,
  estimateScale = 1,
  estimateState = 'ok',
  isPerfAdvisorRegistered = false
}: SupportBundleCreateMswOptions) => [
  http.post(`${SUPPORT_BUNDLES_URL}/estimate-size`, async ({ request }) => {
    if (estimateState === 'loading') {
      await delay('infinite');
    }
    const spec = (await request.json()) as SupportBundleCreateSpec;
    await delay(400);
    return HttpResponse.json(estimateSupportBundleMock(spec, universe, estimateScale));
  }),
  http.post(SUPPORT_BUNDLES_URL, async () => {
    await delay(600);
    const task: YBATask = { task_uuid: 'create-task-uuid', resource_uuid: 'sb-new' };
    return HttpResponse.json(task);
  }),
  http.get('*/api/v1/customers/:customerUuid/universes/:universeUuid/pa_collector', () =>
    isPerfAdvisorRegistered
      ? HttpResponse.json({ success: true })
      : HttpResponse.json({ error: 'Not registered' }, { status: 404 })
  )
];
