import { getTabContent } from './GraphUtils';
import { MetricMeasure, MetricTypes } from '../components/metrics/constants';

// The panel is not under test; stubbing it keeps the import graph off the generated API client.
vi.mock('../components/metrics', () => ({ MetricsPanel: () => null }));

const k8sUniverse = {
  universeDetails: {
    clusters: [
      {
        clusterType: 'PRIMARY',
        userIntent: { providerType: 'kubernetes' }
      }
    ]
  }
};

const makeGraph = (nodeName: string) => ({
  metrics: {},
  prometheusQueryEnabled: false,
  graphFilter: { nodeName, metricMeasure: MetricMeasure.OVERALL }
});

const renderTab = (selectedUniverse: any, nodeName: string, type: string, title: string) =>
  getTabContent(
    makeGraph(nodeName),
    selectedUniverse,
    type,
    [],
    title,
    { data: {} },
    false,
    false,
    () => {},
    false
  );

describe('getTabContent - master pod panel filtering on Kubernetes', () => {
  it.each([
    [MetricTypes.YSQL_OPS, 'YSQL'],
    [MetricTypes.YCQL_OPS, 'YCQL'],
    [MetricTypes.TSERVER, 'Tablet Server'],
    [MetricTypes.TSERVER_TABLE, 'Tablet Server']
  ])('hides %s (%s) panel for yb-master- pods', (type, title) => {
    expect(renderTab(k8sUniverse, 'yb-master-0', type, title)).toBeNull();
  });

  it('keeps master, Resource and YSQL table panels for yb-master- pods', () => {
    expect(renderTab(k8sUniverse, 'yb-master-0', MetricTypes.SERVER, 'Resource')).not.toBeNull();
    // ysql_table shares the 'YSQL' title with ysql_ops, so the filter must key on type.
    expect(
      renderTab(k8sUniverse, 'yb-master-0', MetricTypes.MASTER, 'Master Server')
    ).not.toBeNull();
    expect(
      renderTab(k8sUniverse, 'yb-master-0', MetricTypes.MASTER_ADVANCED, 'Master Server Advanced')
    ).not.toBeNull();
    expect(renderTab(k8sUniverse, 'yb-master-0', 'ysql_table', 'YSQL')).not.toBeNull();
  });

  it('does not hide YSQL panels for yb-tserver- pods', () => {
    expect(renderTab(k8sUniverse, 'yb-tserver-0', MetricTypes.YSQL_OPS, 'YSQL')).not.toBeNull();
  });
});
