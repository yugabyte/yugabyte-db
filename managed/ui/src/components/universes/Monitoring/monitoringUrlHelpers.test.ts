import { describe, expect, it } from 'vitest';
import { URL_TAB_PATH } from '@yugabytedb/perf-advisor-ui';
import { PaRegistrationMode } from '@app/redesign/features/PerfAdvisor/api';
import {
  MetricsSubTab,
  MonitoringItem,
  getLegacyMonitoringRedirect,
  getMonitoringNav,
  getMonitoringRedirect,
  getPerfAdvisorDefaultItem,
  isMonitoringPath
} from './monitoringUrlHelpers';

const U = '1f497805-ecf9-4300-8f99-31afcd250a88';
const ANOMALY = '6a2b5f0e-3c4d-4e5f-8a9b-0c1d2e3f4a5b';
const BASE = `/universes/${U}/monitoring`;

describe('getLegacyMonitoringRedirect', () => {
  it('moves the Metrics tab to Metrics, keeping its query', () => {
    const query = { tab: 'ysql_ops', nodeName: 'n1', filterType: 'custom', startDate: '1' };
    expect(getLegacyMonitoringRedirect(`/universes/${U}/metrics`, query)).toEqual({
      pathname: `${BASE}/metrics/classic`,
      query
    });
  });

  it('moves the Queries tab, keeping its query', () => {
    expect(getLegacyMonitoringRedirect(`/universes/${U}/queries`, { tab: 'slow-queries' })).toEqual(
      {
        pathname: `${BASE}/queries`,
        query: { tab: 'slow-queries' }
      }
    );
  });

  it('keeps Live Queries for an old Queries URL without a tab, its default then', () => {
    expect(getLegacyMonitoringRedirect(`/universes/${U}/queries`, { nodeName: 'n1' })).toEqual({
      pathname: `${BASE}/queries`,
      query: { tab: 'live-queries', nodeName: 'n1' }
    });
    expect(getLegacyMonitoringRedirect(`/universes/${U}/queries`, {})).toEqual({
      pathname: `${BASE}/queries`,
      query: { tab: 'live-queries' }
    });
  });

  it('opens Cluster Load for a bare Performance tab', () => {
    expect(getLegacyMonitoringRedirect(`/universes/${U}/perfAdvisor`, {})).toEqual({
      pathname: `${BASE}/clusterLoad`,
      query: {}
    });
    expect(getLegacyMonitoringRedirect(`/universes/${U}/perfAdvisor/`, {})).toEqual({
      pathname: `${BASE}/clusterLoad`,
      query: {}
    });
  });

  it('turns the Performance tab ?tab= into the path and drops it', () => {
    expect(
      getLegacyMonitoringRedirect(`/universes/${U}/perfAdvisor`, {
        tab: 'metricsNew',
        subTab: 'x',
        duration: '1h'
      })
    ).toEqual({ pathname: `${BASE}/metrics/metricsNew`, query: { duration: '1h' } });
    expect(
      getLegacyMonitoringRedirect(`/universes/${U}/perfAdvisor`, { tab: 'databases' })
    ).toEqual({ pathname: `${BASE}/databases`, query: {} });
    expect(
      getLegacyMonitoringRedirect(`/universes/${U}/perfAdvisor`, { tab: 'clusterLoad' })
    ).toEqual({ pathname: `${BASE}/clusterLoad`, query: {} });
  });

  it('keeps the rest of a Performance tab path', () => {
    const query = { startTime: '1', endTime: '2' };
    expect(
      getLegacyMonitoringRedirect(
        `/universes/${U}/perfAdvisor/clusterLoad/overallLoad/queries`,
        query
      )
    ).toEqual({ pathname: `${BASE}/clusterLoad/overallLoad/queries`, query });
    expect(
      getLegacyMonitoringRedirect(`/universes/${U}/perfAdvisor/databases/yugabyte/tables`, {})
    ).toEqual({ pathname: `${BASE}/databases/yugabyte/tables`, query: {} });
  });

  it('puts the old Performance Metrics view under Metrics', () => {
    expect(
      getLegacyMonitoringRedirect(`/universes/${U}/perfAdvisor/metricsNew`, { metricsTab: 'CPU' })
    ).toEqual({ pathname: `${BASE}/metrics/metricsNew`, query: { metricsTab: 'CPU' } });
  });

  it('keeps Perf Advisor drilldowns', () => {
    const query = { queryId: '123', universeId: U, dbId: '16644' };
    expect(
      getLegacyMonitoringRedirect(
        `/universes/${U}/perfAdvisor/clusterLoad/overallLoad/anomalies/${ANOMALY}/queries/123`,
        query
      )
    ).toEqual({
      pathname: `${BASE}/clusterLoad/overallLoad/anomalies/${ANOMALY}/queries/123`,
      query
    });
    expect(getLegacyMonitoringRedirect(`/universes/${U}/perfAdvisor/${ANOMALY}`, {})).toEqual({
      pathname: `${BASE}/${ANOMALY}`,
      query: {}
    });
  });

  it('leaves other URLs alone', () => {
    expect(getLegacyMonitoringRedirect(`/universes/${U}/overview`, {})).toBeNull();
    expect(getLegacyMonitoringRedirect(`${BASE}/queries`, { tab: 'live-queries' })).toBeNull();
    expect(getLegacyMonitoringRedirect('/metrics', {})).toBeNull();
    expect(getLegacyMonitoringRedirect('/config/perfAdvisor', {})).toBeNull();
    expect(getLegacyMonitoringRedirect(undefined, {})).toBeNull();
  });

  it('never sends a ?tab= to a Perf Advisor view', () => {
    const paPaths = [
      `/universes/${U}/perfAdvisor`,
      `/universes/${U}/perfAdvisor/clusterLoad`,
      `/universes/${U}/perfAdvisor/metricsNew`,
      `/universes/${U}/perfAdvisor/databases`
    ];
    paPaths.forEach((path) => {
      expect(getLegacyMonitoringRedirect(path, { tab: 'clusterLoad' })?.query).not.toHaveProperty(
        'tab'
      );
    });
  });
});

describe('getMonitoringNav', () => {
  it('reads the item and Metrics sub-tab', () => {
    expect(getMonitoringNav(`${BASE}/metrics/classic`)).toEqual({
      item: MonitoringItem.METRICS,
      metricsSubTab: MetricsSubTab.CLASSIC
    });
    expect(getMonitoringNav(`${BASE}/metrics/metricsNew`)).toEqual({
      item: MonitoringItem.METRICS,
      metricsSubTab: MetricsSubTab.ADVANCED
    });
    expect(getMonitoringNav(`${BASE}/metrics`)).toEqual({
      item: MonitoringItem.METRICS,
      metricsSubTab: null
    });
    expect(getMonitoringNav(`${BASE}/queries`).item).toBe(MonitoringItem.QUERIES);
  });

  it('puts Cluster Load and its anomaly drilldowns under Cluster Load', () => {
    [
      `${BASE}/clusterLoad`,
      `${BASE}/clusterLoad/overallLoad/anomalies/${ANOMALY}`,
      `${BASE}/${ANOMALY}`
    ].forEach((path) => expect(getMonitoringNav(path).item).toBe(MonitoringItem.CLUSTER_LOAD));
  });

  it('puts Databases, its tables and its query drilldowns under Databases', () => {
    [
      `${BASE}/databases`,
      `${BASE}/databases/yugabyte/tables`,
      `${BASE}/databases/queries/123`
    ].forEach((path) =>
      expect(getMonitoringNav(path)).toEqual({
        item: MonitoringItem.DATABASES,
        metricsSubTab: null
      })
    );
  });

  it('has no item for a bare or unknown path', () => {
    expect(getMonitoringNav(BASE).item).toBeNull();
    expect(getMonitoringNav(`${BASE}/`).item).toBeNull();
    expect(getMonitoringNav(`${BASE}/unknown`).item).toBeNull();
    expect(getMonitoringNav(`/universes/${U}/overview`).item).toBeNull();
  });
});

describe('isMonitoringPath', () => {
  it('matches only the Monitoring tab', () => {
    expect(isMonitoringPath(BASE)).toBe(true);
    expect(isMonitoringPath(`${BASE}/queries`)).toBe(true);
    expect(isMonitoringPath(`/universes/${U}/monitoringx`)).toBe(false);
    expect(isMonitoringPath(`/universes/${U}/perfAdvisor`)).toBe(false);
  });
});

describe('Perf Advisor path segments', () => {
  it('match the ones Perf Advisor reads from the URL', () => {
    expect(MonitoringItem.CLUSTER_LOAD).toBe(URL_TAB_PATH.CLUSTER_LOAD);
    expect(MonitoringItem.DATABASES).toBe(URL_TAB_PATH.DATABASES);
    expect(MetricsSubTab.ADVANCED).toBe(URL_TAB_PATH.METRICS);
  });
});

describe('getPerfAdvisorDefaultItem', () => {
  it('opens Cluster Load in advanced mode and Databases otherwise', () => {
    expect(getPerfAdvisorDefaultItem(PaRegistrationMode.ADVANCED)).toBe(
      MonitoringItem.CLUSTER_LOAD
    );
    expect(getPerfAdvisorDefaultItem(PaRegistrationMode.BASIC)).toBe(MonitoringItem.DATABASES);
    expect(getPerfAdvisorDefaultItem(undefined)).toBe(MonitoringItem.DATABASES);
  });
});

describe('getMonitoringRedirect', () => {
  const ADVANCED_ITEMS = [
    MonitoringItem.METRICS,
    MonitoringItem.QUERIES,
    MonitoringItem.CLUSTER_LOAD,
    MonitoringItem.DATABASES
  ];
  const BOTH_SUB_TABS = [MetricsSubTab.CLASSIC, MetricsSubTab.ADVANCED];
  const redirect = (
    path: string,
    search = '',
    items = ADVANCED_ITEMS,
    metricsSubTabs = BOTH_SUB_TABS,
    isPerfAdvisorPending = false
  ) =>
    getMonitoringRedirect({
      universeUuid: U,
      pathname: path,
      search,
      items,
      metricsSubTabs,
      isPerfAdvisorPending
    });

  it('leaves a view the universe has, and paths outside Monitoring', () => {
    expect(redirect(`${BASE}/clusterLoad/overallLoad/queries`)).toBeNull();
    expect(redirect(`${BASE}/metrics/metricsNew`)).toBeNull();
    expect(redirect(`/universes/${U}/overview`)).toBeNull();
  });

  it('opens the first view for a bare or unknown path, dropping the query', () => {
    expect(redirect(BASE, '?x=1')).toBe(`${BASE}/metrics/classic`);
    expect(redirect(`${BASE}/unknown`)).toBe(`${BASE}/metrics/classic`);
  });

  it('completes the Metrics path, keeping its query', () => {
    expect(redirect(`${BASE}/metrics`, '?tab=server')).toBe(`${BASE}/metrics/classic?tab=server`);
  });

  it('sends Overview when the universe has no view', () => {
    expect(redirect(BASE, '', [], [])).toBe(`/universes/${U}/overview`);
  });

  it('waits while Perf Advisor loads, since its views may still appear', () => {
    expect(redirect(BASE, '', [], [], true)).toBeNull();
    expect(redirect(`${BASE}/clusterLoad`, '', [MonitoringItem.QUERIES], [], true)).toBeNull();
  });

  it('moves a Perf Advisor view this universe lacks to the one it has, keeping the query', () => {
    const basic = [MonitoringItem.METRICS, MonitoringItem.QUERIES, MonitoringItem.DATABASES];
    expect(redirect(`${BASE}/clusterLoad`, '?duration=1h', basic, [MetricsSubTab.CLASSIC])).toBe(
      `${BASE}/databases?duration=1h`
    );
  });

  it('opens Advanced Metrics without `?tab=` when classic Metrics is hidden', () => {
    const items = [MonitoringItem.METRICS, MonitoringItem.CLUSTER_LOAD];
    expect(
      redirect(`${BASE}/metrics/classic`, '?tab=server&nodeName=n1', items, [
        MetricsSubTab.ADVANCED
      ])
    ).toBe(`${BASE}/metrics/metricsNew?nodeName=n1`);
  });
});
