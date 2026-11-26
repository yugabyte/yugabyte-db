import {
  MetricTypes,
  MetricTypesWithOperations,
  NODE_EXPORTER_PANEL_SOURCES,
  metricsForTab
} from './constants';

const SIDECAR_METRICS = Object.keys(NODE_EXPORTER_PANEL_SOURCES);

describe('metricsForTab', () => {
  it('returns the tab keys unchanged on VMs for non-master views', () => {
    expect(metricsForTab(MetricTypes.SERVER, false, false, true)).toEqual(
      MetricTypesWithOperations[MetricTypes.SERVER].metrics
    );
    expect(metricsForTab(MetricTypes.DISK_IO, false, false, true)).toEqual(
      MetricTypesWithOperations[MetricTypes.DISK_IO].metrics
    );
  });

  it('swaps the Resource tab to Kubernetes keys and adds the sidecar memory panel last', () => {
    const metrics = metricsForTab(MetricTypes.SERVER, true, false, true);
    expect(metrics).toContain('k8s_cpu_usage');
    expect(metrics).toContain('k8s_memory_usage');
    expect(metrics).toContain('k8s_volume_disk_usage_percent');
    expect(metrics).toContain('k8s_volume_disk_used_size_total');
    expect(metrics).toContain('k8s_volume_disk_volume_usage_percent');
    expect(metrics).toContain('k8s_volume_disk_volume_used');
    expect(metrics).not.toContain('cpu_usage');
    expect(metrics).not.toContain('memory_usage');
    expect(metrics).not.toContain('system_load_over_time');
    expect(metrics).toContain('disk_iops');
    expect(metrics).toContain('node_clock_skew');
    expect(metrics[metrics.length - 1]).toBe('k8s_node_exporter_memory_usage');
    expect(metrics).toHaveLength(MetricTypesWithOperations[MetricTypes.SERVER].metrics.length);
  });

  it('keeps the sidecar memory panel off VMs and off other tabs', () => {
    expect(metricsForTab(MetricTypes.SERVER, false, false, true)).not.toContain(
      'k8s_node_exporter_memory_usage'
    );
    expect(metricsForTab(MetricTypes.DISK_IO, true, false, true)).not.toContain(
      'k8s_node_exporter_memory_usage'
    );
  });

  it('swaps the Disk I/O tab DB keys to master keys for masters', () => {
    const metrics = metricsForTab(MetricTypes.DISK_IO, false, true, true);
    expect(metrics).toEqual(
      expect.arrayContaining([
        'master_lsm_rocksdb_flush_size',
        'master_lsm_rocksdb_compaction',
        'master_lsm_rocksdb_compaction_time',
        'master_log_bytes_read',
        'master_log_bytes_written',
        'disk_iops'
      ])
    );
    expect(metrics).not.toContain('lsm_rocksdb_flush_size');
    expect(metrics).not.toContain('tserver_log_bytes_read');
  });

  it('applies both swaps to master Disk I/O on Kubernetes', () => {
    const metrics = metricsForTab(MetricTypes.DISK_IO, true, true, true);
    expect(metrics).toContain('k8s_volume_disk_usage_percent');
    expect(metrics).toContain('master_lsm_rocksdb_flush_size');
    expect(metrics).not.toContain('disk_usage_percent');
  });

  it('drops the sidecar panels on Kubernetes when the sidecar is not scraped', () => {
    const resource = metricsForTab(MetricTypes.SERVER, true, false, false);
    SIDECAR_METRICS.forEach((metric) => expect(resource).not.toContain(metric));
    expect(resource).not.toContain('k8s_node_exporter_memory_usage');
    expect(resource).toEqual([
      'k8s_cpu_usage',
      'k8s_memory_usage',
      'k8s_volume_disk_usage_percent',
      'k8s_volume_disk_used_size_total',
      'k8s_volume_disk_volume_usage_percent',
      'k8s_volume_disk_volume_used',
      'node_clock_skew'
    ]);

    const diskIo = metricsForTab(MetricTypes.DISK_IO, true, false, false);
    SIDECAR_METRICS.forEach((metric) => expect(diskIo).not.toContain(metric));
    expect(diskIo).toContain('k8s_volume_disk_usage_percent');
    expect(diskIo).toContain('lsm_rocksdb_flush_size');
  });

  it('keeps the master swap on Kubernetes when the sidecar is not scraped', () => {
    const metrics = metricsForTab(MetricTypes.DISK_IO, true, true, false);
    expect(metrics).toContain('master_lsm_rocksdb_flush_size');
    expect(metrics).not.toContain('lsm_rocksdb_flush_size');
    expect(metrics).not.toContain('disk_iops');
  });

  it('ignores the sidecar flag on VMs', () => {
    expect(metricsForTab(MetricTypes.SERVER, false, false, false)).toEqual(
      metricsForTab(MetricTypes.SERVER, false, false, true)
    );
    expect(metricsForTab(MetricTypes.DISK_IO, false, true, false)).toEqual(
      metricsForTab(MetricTypes.DISK_IO, false, true, true)
    );
  });

  it('does not change the shared tab definition', () => {
    const before = [...MetricTypesWithOperations[MetricTypes.SERVER].metrics];
    metricsForTab(MetricTypes.SERVER, true, true, true);
    expect(MetricTypesWithOperations[MetricTypes.SERVER].metrics).toEqual(before);
  });
});
