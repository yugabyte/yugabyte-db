import { MetricTypes, MetricTypesWithOperations, metricsForTab } from './constants';

describe('metricsForTab', () => {
  it('returns the tab keys unchanged on VMs for non-master views', () => {
    expect(metricsForTab(MetricTypes.SERVER, false, false)).toEqual(
      MetricTypesWithOperations[MetricTypes.SERVER].metrics
    );
    expect(metricsForTab(MetricTypes.DISK_IO, false, false)).toEqual(
      MetricTypesWithOperations[MetricTypes.DISK_IO].metrics
    );
  });

  it('swaps the Resource tab to Kubernetes keys and adds the sidecar memory panel last', () => {
    const metrics = metricsForTab(MetricTypes.SERVER, true, false);
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
    expect(metricsForTab(MetricTypes.SERVER, false, false)).not.toContain(
      'k8s_node_exporter_memory_usage'
    );
    expect(metricsForTab(MetricTypes.DISK_IO, true, false)).not.toContain(
      'k8s_node_exporter_memory_usage'
    );
  });

  it('swaps the Disk I/O tab DB keys to master keys for masters', () => {
    const metrics = metricsForTab(MetricTypes.DISK_IO, false, true);
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
    const metrics = metricsForTab(MetricTypes.DISK_IO, true, true);
    expect(metrics).toContain('k8s_volume_disk_usage_percent');
    expect(metrics).toContain('master_lsm_rocksdb_flush_size');
    expect(metrics).not.toContain('disk_usage_percent');
  });

  it('does not change the shared tab definition', () => {
    const before = [...MetricTypesWithOperations[MetricTypes.SERVER].metrics];
    metricsForTab(MetricTypes.SERVER, true, true);
    expect(MetricTypesWithOperations[MetricTypes.SERVER].metrics).toEqual(before);
  });
});
