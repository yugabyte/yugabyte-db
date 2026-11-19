import { getNodeExporterPanelNotes } from './NodeExporterNotes';

const nodes = [
  {
    nodeName: 'yb-tserver-0_az-b',
    cloudInfo: { kubernetesPodName: 'yb-tserver-0', az: 'az-b' }
  },
  {
    nodeName: 'yb-tserver-1_az-a',
    cloudInfo: { kubernetesPodName: 'yb-tserver-1', az: 'az-a' }
  }
];

const universeOn = (providerType: string) => ({
  universeDetails: {
    clusters: [{ clusterType: 'PRIMARY', userIntent: { providerType } }],
    nodeDetailsSet: nodes
  }
});

const k8sUniverse = universeOn('kubernetes');

const checkSeries = (instanceName: string, labels: Record<string, string>) => ({
  instanceName,
  labels,
  x: [1, 2],
  y: ['1', '1']
});

const tabMetrics = {
  node_exporter_check: {
    data: [
      checkSeries('yb-tserver-0_az-b', {
        check: 'data_volume',
        target: '/mnt/disk0',
        reason: 'bind_mount'
      }),
      checkSeries('yb-tserver-1_az-a', {
        check: 'data_volume',
        target: '/mnt/disk0',
        reason: 'ok'
      }),
      checkSeries('yb-tserver-1_az-a', { check: 'diskstats', reason: 'overlaid_overlay' }),
      checkSeries('yb-tserver-1_az-a', {
        check: 'data_volume',
        target: '/mnt/disk0',
        reason: 'diskstats_overlaid'
      })
    ]
  },
  node_exporter_collector_success: {
    data: [
      { instanceName: 'yb-tserver-0_az-b', name: 'netstat', x: [1, 2], y: ['1', '0'] },
      { instanceName: 'yb-tserver-1_az-a', name: 'netdev', x: [1, 2], y: ['1', '1'] }
    ]
  }
};

describe('getNodeExporterPanelNotes', () => {
  it('lists failed disk checks per pod, ordered by zone', () => {
    expect(getNodeExporterPanelNotes('disk_iops', tabMetrics, k8sUniverse)).toEqual([
      'yb-tserver-1 (az-a): /proc/diskstats is overlaid (overlay), so disk statistics are unavailable',
      'yb-tserver-0 (az-b): the data volume is a bind mount of a node directory, not a dedicated volume (/mnt/disk0)'
    ]);
  });

  it('notes a collector that failed at any point in the range, only on its panels', () => {
    expect(getNodeExporterPanelNotes('network_tcp_retransmits', tabMetrics, k8sUniverse)).toEqual([
      'yb-tserver-0 (az-b): the netstat collector failed'
    ]);
    expect(getNodeExporterPanelNotes('network_bytes', tabMetrics, k8sUniverse)).toEqual([]);
  });

  it('adds nothing to panels the sidecar does not feed', () => {
    expect(getNodeExporterPanelNotes('k8s_cpu_usage', tabMetrics, k8sUniverse)).toEqual([]);
    expect(getNodeExporterPanelNotes('lsm_rocksdb_flush_size', tabMetrics, k8sUniverse)).toEqual([]);
  });

  it('adds nothing on a non-Kubernetes universe', () => {
    expect(getNodeExporterPanelNotes('disk_iops', tabMetrics, universeOn('aws'))).toEqual([]);
    expect(getNodeExporterPanelNotes('disk_iops', tabMetrics, undefined)).toEqual([]);
  });

  it('adds nothing when the check series are absent', () => {
    expect(getNodeExporterPanelNotes('disk_iops', {}, k8sUniverse)).toEqual([]);
  });

  it('falls back to the node name and a raw reason it does not know', () => {
    const metrics = {
      node_exporter_check: {
        data: [checkSeries('unknown-node', { check: 'data_volume', reason: 'new_reason' })]
      }
    };
    expect(getNodeExporterPanelNotes('disk_iops', metrics, k8sUniverse)).toEqual([
      'unknown-node: new_reason'
    ]);
  });
});
