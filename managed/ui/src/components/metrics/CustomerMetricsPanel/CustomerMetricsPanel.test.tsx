import { PanelBody } from './CustomerMetricsPanel';
import { MetricMeasure, MetricOrigin, MetricTypes, NodeType } from '../constants';

// The tabs render GraphTab, which is not under test.
vi.mock('../GraphTab/GraphTab', () => ({ GraphTab: () => null }));
vi.mock('../../metrics', () => ({ GraphPanelHeaderContainer: () => null }));

const universe = (providerType: string) => ({
  universeUUID: 'u1',
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

const tabKeysFor = (selectedUniverse: any, type: string, nodeName = 'all'): string[] => {
  const tab = tabElements(renderBody(selectedUniverse, NodeType.ALL, nodeName)).find(
    (t: any) => t.props.eventKey === type
  );
  return tab.props.children.props.metricsKey;
};

describe('PanelBody tabs on Kubernetes', () => {
  it('shows Resource, Disk I/O and Container, and hides Per Process', () => {
    const tabs = tabsFor(universe('kubernetes'));
    expect(tabs).toContain(MetricTypes.SERVER);
    expect(tabs).toContain(MetricTypes.DISK_IO);
    expect(tabs).toContain(MetricTypes.CONTAINER);
    expect(tabs).not.toContain(MetricTypes.PER_PROCESS);
  });

  it('shows Disk I/O for the Master node type', () => {
    expect(tabsFor(universe('kubernetes'), NodeType.MASTER)).toContain(MetricTypes.DISK_IO);
  });

  it('queries the Kubernetes keys on Resource, without the panels pods have no source for', () => {
    const keys = tabKeysFor(universe('kubernetes'), MetricTypes.SERVER);
    expect(keys).toContain('k8s_cpu_usage');
    expect(keys).toContain('k8s_volume_disk_volume_used');
    expect(keys).not.toContain('cpu_usage');
    expect(keys).not.toContain('disk_iops');
    expect(keys).not.toContain('network_bytes');
    expect(keys).not.toContain('system_load_over_time');
  });
});

describe('PanelBody tabs on VMs', () => {
  it('shows Resource and Disk I/O, and hides Container and Per Process', () => {
    const tabs = tabsFor(universe('aws'));
    expect(tabs).toContain(MetricTypes.SERVER);
    expect(tabs).toContain(MetricTypes.DISK_IO);
    expect(tabs).not.toContain(MetricTypes.CONTAINER);
    expect(tabs).not.toContain(MetricTypes.PER_PROCESS);
  });

  it('shows Disk I/O for the Master node type', () => {
    expect(tabsFor(universe('aws'), NodeType.MASTER)).toContain(MetricTypes.DISK_IO);
  });

  it('keeps the VM keys on Resource', () => {
    const keys = tabKeysFor(universe('aws'), MetricTypes.SERVER);
    expect(keys).toContain('cpu_usage');
    expect(keys).toContain('disk_iops');
    expect(keys).not.toContain('k8s_cpu_usage');
  });
});

describe('PanelBody Disk I/O keys for a selected node', () => {
  it('reads the master series for a selected Kubernetes master pod', () => {
    const keys = tabKeysFor(universe('kubernetes'), MetricTypes.DISK_IO, 'yb-master-0');
    expect(keys).toContain('master_lsm_rocksdb_flush_size');
    expect(keys).not.toContain('lsm_rocksdb_flush_size');
  });

  it('keeps the tserver series for a VM node whose name contains yb-master-', () => {
    const keys = tabKeysFor(universe('aws'), MetricTypes.DISK_IO, 'yb-15-yb-master-prod-n3');
    expect(keys).toContain('lsm_rocksdb_flush_size');
    expect(keys).not.toContain('master_lsm_rocksdb_flush_size');
  });
});
