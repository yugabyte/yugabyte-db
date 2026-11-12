import { URL_TAB_PATH } from '@yugabytedb/perf-advisor-ui';
import { MONITORING_PATH, PERF_ADVISOR_PATH } from '@app/redesign/helpers/constants';

/**
 * URLs of the universe Monitoring tab, and redirects from the Metrics, Queries and Performance
 * tabs it replaced.
 *
 *   /universes/<uuid>/monitoring/metrics/classic        Metrics, `?tab=` picks the group
 *   /universes/<uuid>/monitoring/metrics/metricsNew     Advanced Metrics (Perf Advisor)
 *   /universes/<uuid>/monitoring/queries                Queries, `?tab=` picks the tab
 *   /universes/<uuid>/monitoring/clusterLoad/...        Cluster Load (Perf Advisor)
 *   /universes/<uuid>/monitoring/databases/...          Databases (Perf Advisor)
 *   /universes/<uuid>/monitoring/<anomalyUuid>/...      Perf Advisor drilldown, under Cluster Load
 *
 * Perf Advisor views read their state from the URL and write it with `window.history`, stripping
 * its own segments (`clusterLoad`, `databases`, `metricsNew`, `queries`, ...) wherever they occur.
 * So Perf Advisor is mounted only under the paths above, and never with a `?tab=` query, which it
 * reads before the path.
 */

// CLUSTER_LOAD, DATABASES and ADVANCED are Perf Advisor's own path segments (its URL_TAB_PATH).
export const MonitoringItem = {
  METRICS: 'metrics',
  QUERIES: 'queries',
  CLUSTER_LOAD: 'clusterLoad',
  DATABASES: 'databases'
} as const;
export type MonitoringItem = (typeof MonitoringItem)[keyof typeof MonitoringItem];

export const MetricsSubTab = {
  CLASSIC: 'classic',
  ADVANCED: 'metricsNew'
} as const;
export type MetricsSubTab = (typeof MetricsSubTab)[keyof typeof MetricsSubTab];

type Query = Record<string, string | string[] | undefined>;

export interface MonitoringLocation {
  pathname: string;
  query: Query;
}

export interface MonitoringNav {
  item: MonitoringItem | null;
  metricsSubTab: MetricsSubTab | null;
}

const UUID_PATTERN = /^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/i;

export const getMonitoringBasePath = (universeUuid: string) =>
  `/universes/${universeUuid}/${MONITORING_PATH}`;

export const getMonitoringItemPath = (
  universeUuid: string,
  item: MonitoringItem,
  metricsSubTab: MetricsSubTab = MetricsSubTab.CLASSIC
) =>
  item === MonitoringItem.METRICS
    ? `${getMonitoringBasePath(universeUuid)}/${item}/${metricsSubTab}`
    : `${getMonitoringBasePath(universeUuid)}/${item}`;

/** The path segments after `/universes/<uuid>/monitoring`, or null outside the Monitoring tab. */
const getMonitoringSegments = (pathname: string | undefined): string[] | null => {
  const match = pathname?.match(new RegExp(`^/universes/[^/]+/${MONITORING_PATH}(?:/(.*))?$`));
  return match ? (match[1] ?? '').split('/').filter(Boolean) : null;
};

export const isMonitoringPath = (pathname: string | undefined): boolean =>
  getMonitoringSegments(pathname) !== null;

export const getMonitoringNav = (pathname: string | undefined): MonitoringNav => {
  const [first, second] = getMonitoringSegments(pathname) ?? [];
  if (first === MonitoringItem.METRICS) {
    const metricsSubTab =
      second === MetricsSubTab.CLASSIC || second === MetricsSubTab.ADVANCED ? second : null;
    return { item: MonitoringItem.METRICS, metricsSubTab };
  }
  if (first === MonitoringItem.QUERIES) {
    return { item: MonitoringItem.QUERIES, metricsSubTab: null };
  }
  if (first === URL_TAB_PATH.DATABASES) {
    return { item: MonitoringItem.DATABASES, metricsSubTab: null };
  }
  if (first === URL_TAB_PATH.CLUSTER_LOAD || (first !== undefined && UUID_PATTERN.test(first))) {
    return { item: MonitoringItem.CLUSTER_LOAD, metricsSubTab: null };
  }
  return { item: null, metricsSubTab: null };
};

export const PERF_ADVISOR_ITEMS: MonitoringItem[] = [
  MonitoringItem.CLUSTER_LOAD,
  MonitoringItem.DATABASES
];

const isPerfAdvisorItem = (item: MonitoringItem | null): item is MonitoringItem =>
  item !== null && PERF_ADVISOR_ITEMS.includes(item);

/** Whether this universe has the view a Monitoring path shows. */
export const isMonitoringNavShown = (
  { item, metricsSubTab }: MonitoringNav,
  items: MonitoringItem[],
  metricsSubTabs: MetricsSubTab[]
): boolean =>
  item !== null &&
  items.includes(item) &&
  (item !== MonitoringItem.METRICS ||
    (metricsSubTab !== null && metricsSubTabs.includes(metricsSubTab)));

interface MonitoringRedirectParams {
  universeUuid: string;
  pathname: string | undefined;
  search: string;
  items: MonitoringItem[];
  metricsSubTabs: MetricsSubTab[];
  isPerfAdvisorPending: boolean;
}

/**
 * Where a Monitoring path this universe can't show goes instead: its first view, or Overview when
 * it has none. A Perf Advisor view falls back to the other Perf Advisor view first. While Perf
 * Advisor's state loads, its views and so the answer may still change, so nothing redirects.
 */
export const getMonitoringRedirect = ({
  universeUuid,
  pathname,
  search,
  items,
  metricsSubTabs,
  isPerfAdvisorPending
}: MonitoringRedirectParams): string | null => {
  if (!isMonitoringPath(pathname)) {
    return null;
  }
  const nav = getMonitoringNav(pathname);
  if (isMonitoringNavShown(nav, items, metricsSubTabs) || isPerfAdvisorPending) {
    return null;
  }
  if (!items.length) {
    return `/universes/${universeUuid}/overview`;
  }
  const { item } = nav;
  const fallback =
    (isPerfAdvisorItem(item) ? items.find(isPerfAdvisorItem) : undefined) ?? items[0];
  const target = item !== null && items.includes(item) ? item : fallback;
  const keepsQuery = target === item || (isPerfAdvisorItem(item) && isPerfAdvisorItem(target));
  const query = new URLSearchParams(keepsQuery ? search : '');
  // Perf Advisor reads `?tab=` before the path.
  if (target === MonitoringItem.METRICS && metricsSubTabs[0] === MetricsSubTab.ADVANCED) {
    query.delete('tab');
  }
  const queryString = query.toString();
  return `${getMonitoringItemPath(universeUuid, target, metricsSubTabs[0])}${
    queryString ? `?${queryString}` : ''
  }`;
};

/**
 * Where an old Metrics, Queries or Performance tab URL now lives, keeping the rest of the path and
 * the query, or null for any other URL. Bookmarks and shared links still use these URLs; keep this
 * while the routes exist.
 */
export const getLegacyMonitoringRedirect = (
  pathname: string | undefined,
  query: Query = {}
): MonitoringLocation | null => {
  const match = pathname?.match(
    new RegExp(`^/universes/([^/]+)/(metrics|queries|${PERF_ADVISOR_PATH})(?:/(.*))?$`)
  );
  if (!match) {
    return null;
  }
  const [, universeUuid, tab, rest] = match;
  const basePath = getMonitoringBasePath(universeUuid);

  if (tab === 'metrics') {
    return { pathname: getMonitoringItemPath(universeUuid, MonitoringItem.METRICS), query };
  }
  if (tab === 'queries') {
    // Old Queries URLs without `?tab=` opened Live Queries.
    return {
      pathname: getMonitoringItemPath(universeUuid, MonitoringItem.QUERIES),
      query: { tab: 'live-queries', ...query }
    };
  }

  // The Performance tab took `?tab=` (and `subTab`) as another way to pick its view. Perf Advisor
  // reads `?tab=` before the path, so it must not survive the redirect.
  const { tab: queryTab, subTab, ...restQuery } = query;
  const restPath = rest?.replace(/\/+$/, '');
  if (!restPath) {
    if (queryTab === URL_TAB_PATH.METRICS) {
      return {
        pathname: getMonitoringItemPath(
          universeUuid,
          MonitoringItem.METRICS,
          MetricsSubTab.ADVANCED
        ),
        query: restQuery
      };
    }
    if (queryTab === URL_TAB_PATH.DATABASES) {
      return { pathname: `${basePath}/${URL_TAB_PATH.DATABASES}`, query: restQuery };
    }
    return { pathname: `${basePath}/${URL_TAB_PATH.CLUSTER_LOAD}`, query: restQuery };
  }
  if (restPath === URL_TAB_PATH.METRICS || restPath.startsWith(`${URL_TAB_PATH.METRICS}/`)) {
    return { pathname: `${basePath}/${MonitoringItem.METRICS}/${restPath}`, query: restQuery };
  }
  return { pathname: `${basePath}/${restPath}`, query: restQuery };
};
