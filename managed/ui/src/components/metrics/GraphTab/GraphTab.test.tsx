import { render } from '@testing-library/react';
import { GraphTab } from './GraphTab';
import { MetricMeasure, MetricTypes, NodeType } from '../constants';

const queryMetrics = vi.fn((params: any) => ({ type: 'QUERY_METRICS', params }));
vi.mock('../../../actions/graph', () => ({
  resetMetrics: () => ({ type: 'RESET_METRICS' }),
  queryMetrics: (params: any) => queryMetrics(params),
  queryMetricsSuccess: () => ({ type: 'QUERY_METRICS_SUCCESS' }),
  queryMetricsFailure: () => ({ type: 'QUERY_METRICS_FAILURE' }),
  currentTabSelected: () => ({ type: 'SELECTED_METRIC_TYPE_TAB' })
}));
vi.mock('../../../utils/GraphUtils', () => ({ getTabContent: () => null }));

let state: any;
vi.mock('react-redux', () => ({
  useSelector: (selector: any) => selector(state),
  useDispatch: () => (action: any) => Promise.resolve({ payload: {}, error: false, ...action })
}));

const stateFor = (currentSelectedNodeType: string) => ({
  customer: { currentUser: { data: {} } },
  graph: {
    metrics: {},
    graphFilter: {
      startMoment: 1000,
      endMoment: 2000,
      nodeName: 'all',
      currentSelectedNodeType,
      nodePrefix: 'yb-1',
      metricMeasure: MetricMeasure.OVERALL
    }
  }
});

const kubernetesUniverse = (nodeExporterSidecarScraped: boolean) => ({
  universeUUID: 'u1',
  nodeExporterSidecarScraped,
  universeDetails: {
    clusters: [{ clusterType: 'PRIMARY', userIntent: { providerType: 'kubernetes' } }]
  }
});

const renderTab = (selectedUniverse: any = null) => (
  <GraphTab
    type={MetricTypes.DISK_IO}
    metricsKey={['disk_iops']}
    nodePrefixes={'' as any}
    selectedUniverse={selectedUniverse}
    title="Disk I/O"
    isGranularMetricsEnabled={false}
    isMetricsTimezoneEnabled={false}
    printMode={false}
  />
);

describe('GraphTab', () => {
  it('re-queries when the node type changes', () => {
    state = stateFor(NodeType.ALL);
    const { rerender } = render(renderTab());
    expect(queryMetrics).toHaveBeenCalledTimes(1);
    expect(queryMetrics.mock.calls[0][0].serverType).toBeUndefined();

    state = stateFor(NodeType.MASTER);
    rerender(renderTab());
    expect(queryMetrics).toHaveBeenCalledTimes(2);
    expect(queryMetrics.mock.calls[1][0].serverType).toBe('MASTER');
  });

  it.each([
    [true, true],
    [false, false]
  ])(
    'queries the sidecar checks on Kubernetes only when the sidecar is scraped (%s)',
    (scraped, queried) => {
      queryMetrics.mockClear();
      state = stateFor(NodeType.ALL);
      render(renderTab(kubernetesUniverse(scraped)));
      const metrics = queryMetrics.mock.calls[0][0].metricsWithSettings.map((m: any) => m.metric);
      expect(metrics.includes('node_exporter_check')).toBe(queried);
      expect(metrics.includes('node_exporter_collector_success')).toBe(queried);
    }
  );
});
