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

  it('swaps the Resource tab to Kubernetes keys and drops panels with no Kubernetes source', () => {
    expect(metricsForTab(MetricTypes.SERVER, true, false)).toEqual([
      'k8s_cpu_usage',
      'k8s_memory_usage',
      'k8s_volume_disk_usage_percent',
      'k8s_volume_disk_used_size_total',
      'k8s_volume_disk_volume_usage_percent',
      'k8s_volume_disk_volume_used',
      'node_clock_skew'
    ]);
  });

  it('keeps the DB panels on the Kubernetes Disk I/O tab', () => {
    expect(metricsForTab(MetricTypes.DISK_IO, true, false)).toEqual([
      'k8s_volume_disk_usage_percent',
      'k8s_volume_disk_used_size_total',
      'k8s_volume_disk_volume_usage_percent',
      'k8s_volume_disk_volume_used',
      'lsm_rocksdb_flush_size',
      'lsm_rocksdb_compaction',
      'lsm_rocksdb_compaction_time',
      'tserver_log_bytes_read',
      'tserver_log_bytes_written'
    ]);
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
    expect(metrics).not.toContain('disk_iops');
  });

  it('does not change the shared tab definition', () => {
    const before = [...MetricTypesWithOperations[MetricTypes.SERVER].metrics];
    metricsForTab(MetricTypes.SERVER, true, true);
    expect(MetricTypesWithOperations[MetricTypes.SERVER].metrics).toEqual(before);
  });
});
