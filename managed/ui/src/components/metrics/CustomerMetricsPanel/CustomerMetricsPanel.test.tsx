import { PanelBody } from './CustomerMetricsPanel';
import { MetricMeasure, MetricOrigin, MetricTypes, NodeType } from '../constants';

// The tabs render GraphTab, which is not under test.
vi.mock('../GraphTab/GraphTab', () => ({ GraphTab: () => null }));
vi.mock('../../metrics', () => ({ GraphPanelHeaderContainer: () => null }));

const universe = (providerType: string, nodeExporterSidecarScraped?: boolean) => ({
  universeUUID: 'u1',
  nodeExporterSidecarScraped,
  universeDetails: {
    clusters: [{ clusterType: 'PRIMARY', userIntent: { providerType, enableYSQL: true } }],
    nodeDetailsSet: []
  }
});

const renderBody = (
  selectedUniverse: any,
  currentSelectedNodeType: string = NodeType.ALL,
  nodeName = 'all'
) =>
  PanelBody({
    origin: MetricOrigin.UNIVERSE,
    selectedUniverse,
    nodePrefixes: ['yb-1'],
    width: 1000,
    tableName: null,
    graph: {
      graphFilter: {
        metricMeasure: MetricMeasure.OVERALL,
        currentSelectedNodeType,
        nodeName
      }
    },
    customer: { runtimeConfigs: { data: { configEntries: [] } } },
    printMode: false
  } as any) as any;

const tabElements = (body: any): any[] => ([] as any[]).concat(body.props.children);

const tabsFor = (selectedUniverse: any, currentSelectedNodeType: string = NodeType.ALL) =>
  tabElements(renderBody(selectedUniverse, currentSelectedNodeType)).map(
    (tab: any) => tab.props.eventKey
  );

const diskIoKeysFor = (selectedUniverse: any, nodeName: string): string[] => {
  const tab = tabElements(renderBody(selectedUniverse, NodeType.ALL, nodeName)).find(
    (t: any) => t.props.eventKey === MetricTypes.DISK_IO
  );
  return tab.props.children.props.metricsKey;
};

describe('PanelBody tabs on Kubernetes', () => {
  it('shows Resource and Disk I/O when YBA scrapes the node_exporter sidecar', () => {
    const tabs = tabsFor(universe('kubernetes', true));
    expect(tabs).toContain(MetricTypes.SERVER);
    expect(tabs).toContain(MetricTypes.DISK_IO);
    expect(tabs).toContain(MetricTypes.CONTAINER);
    expect(tabs).not.toContain(MetricTypes.PER_PROCESS);
  });

  it.each([false, undefined])(
    'hides Resource and Disk I/O when the sidecar is not scraped (%s)',
    (scraped) => {
      const tabs = tabsFor(universe('kubernetes', scraped));
      expect(tabs).not.toContain(MetricTypes.SERVER);
      expect(tabs).not.toContain(MetricTypes.DISK_IO);
      expect(tabs).toContain(MetricTypes.CONTAINER);
      expect(tabs).not.toContain(MetricTypes.PER_PROCESS);
    }
  );

  it('shows Disk I/O for the Master node type', () => {
    expect(tabsFor(universe('kubernetes', true), NodeType.MASTER)).toContain(MetricTypes.DISK_IO);
  });
});

describe('PanelBody tabs on VMs', () => {
  it.each([true, false])('ignores the sidecar flag (%s)', (scraped) => {
    const tabs = tabsFor(universe('aws', scraped));
    expect(tabs).toContain(MetricTypes.SERVER);
    expect(tabs).toContain(MetricTypes.DISK_IO);
    expect(tabs).not.toContain(MetricTypes.CONTAINER);
    expect(tabs).not.toContain(MetricTypes.PER_PROCESS);
  });

  it('shows Disk I/O for the Master node type', () => {
    expect(tabsFor(universe('aws'), NodeType.MASTER)).toContain(MetricTypes.DISK_IO);
  });
});

describe('PanelBody Disk I/O keys for a selected node', () => {
  it('reads the master series for a selected Kubernetes master pod', () => {
    const keys = diskIoKeysFor(universe('kubernetes', true), 'yb-master-0');
    expect(keys).toContain('master_lsm_rocksdb_flush_size');
    expect(keys).not.toContain('lsm_rocksdb_flush_size');
  });

  it('keeps the tserver series for a VM node whose name contains yb-master-', () => {
    const keys = diskIoKeysFor(universe('aws'), 'yb-15-yb-master-prod-n3');
    expect(keys).toContain('lsm_rocksdb_flush_size');
    expect(keys).not.toContain('master_lsm_rocksdb_flush_size');
  });
});
