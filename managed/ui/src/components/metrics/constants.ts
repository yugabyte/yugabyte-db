export const MetricOrigin = {
  TABLE: 'table',
  CUSTOMER: 'customer',
  UNIVERSE: 'universe'
} as const;

export const MetricTypes = {
  YSQL_OPS: 'ysql_ops',
  YCQL_OPS: 'ycql_ops',
  SERVER: 'server',
  DISK_IO: 'disk_io',
  PER_PROCESS: 'per_process',
  TSERVER: 'tserver',
  MASTER: 'master',
  MASTER_ADVANCED: 'master_advanced',
  LSMDB: 'lsmdb',
  CONTAINER: 'container',
  TSERVER_TABLE: 'tserver_table',
  LSMDB_TABLE: 'lsmdb_table',
  OUTLIER_TABLES: 'outlier_tables',
  OUTLIER_DATABASES: 'outlier_databases'
} as const;

export const MetricTypesWithOperations = {
  ysql_ops: {
    title: 'YSQL',
    metrics: [
      'ysql_server_rpc_per_second',
      'ysql_sql_latency',
      'ysql_connections',
      'ysql_connections_per_sec',
      'ysql_server_advanced_rpc_per_second',
      'ysql_sql_advanced_latency',
      'ysql_catalog_cache_misses',
      'ysql_conn_mgr_active_connections'
      // TODO(bogdan): Add these in once we have histogram support, see #3630.
      // "ysql_server_rpc_p99"
    ]
  },
  ycql_ops: {
    title: 'YCQL',
    metrics: [
      'cql_server_rpc_per_second',
      'cql_sql_latency',
      'cql_server_rpc_p99',
      'cql_sql_latency_breakdown',
      'cql_yb_local_vs_remote',
      'cql_yb_latency',
      'cql_reactor_latency',
      'tserver_rpc_queue_size_cql',
      'response_sizes',
      'cql_yb_transaction',
      'cql_yb_rpc_connections'
    ]
  },
  server: {
    title: 'Resource',
    metrics: [
      'cpu_usage',
      'memory_usage',
      'disk_iops',
      'disk_usage_percent',
      'disk_used_size_total',
      'disk_volume_usage_percent',
      'disk_volume_used',
      'disk_bytes_per_second_per_node',
      'disk_io_time',
      'disk_io_queue_depth',
      'disk_io_read_latency',
      'disk_io_write_latency',
      'network_packets',
      'network_bytes',
      'network_errors',
      'system_load_over_time',
      'node_clock_skew'
    ]
  },
  per_process: {
    title: 'Per Process',
    metrics: [
      'process_user_cpu_seconds',
      'process_system_cpu_seconds',
      'process_virtual_memory',
      'process_resident_memory',
      'process_proportional_memory',
      'process_io_read',
      'process_io_write',
      'process_open_files'
    ]
  },
  disk_io: {
    title: 'Disk I/O',
    metrics: [
      'disk_iops',
      'disk_usage_percent',
      'disk_used_size_total',
      'disk_volume_usage_percent',
      'disk_volume_used',
      'disk_bytes_per_second_per_node',
      'lsm_rocksdb_flush_size',
      'lsm_rocksdb_compaction',
      'lsm_rocksdb_compaction_time',
      'tserver_log_bytes_read',
      'tserver_log_bytes_written'
    ]
  },
  tserver: {
    title: 'Tablet Server',
    metrics: [
      'tserver_rpcs_per_sec_per_node',
      'tserver_ops_latency',
      'tserver_ops_latency_p99',
      'tserver_handler_latency',
      'tserver_threads_running',
      'tserver_threads_started',
      'tserver_uptime_min',
      'tserver_consensus_rpcs_per_sec',
      'tserver_change_config',
      'tserver_remote_bootstraps',
      'tserver_remote_bootstrap_bandwidth',
      'tserver_consensus_rpcs_latency',
      'tserver_change_config_latency',
      'tserver_context_switches',
      'tserver_spinlock_server',
      'tserver_log_latency',
      'tserver_log_bytes_written',
      'tserver_log_bytes_read',
      'tserver_log_ops_second',
      'tserver_write_lock_latency',
      'tserver_tc_malloc_stats',
      'tserver_log_stats',
      'tserver_cache_reader_num_ops',
      'tserver_glog_info_messages',
      'tserver_rpc_queue_size_tserver',
      'tserver_cpu_util_secs',
      'tserver_yb_rpc_connections',
      'tserver_live_tablet_peers',
      'raft_leader',
      'tserver_max_follower_lag'
    ]
  },
  master: {
    title: 'Master Server',
    metrics: [
      'master_overall_rpc_rate',
      'master_latency',
      'master_uptime_min',
      'master_get_tablet_location',
      'master_tsservice_reads',
      'master_tsservice_reads_latency',
      'master_tsservice_writes',
      'master_tsservice_writes_latency',
      'master_ts_heartbeats',
      'tserver_rpc_queue_size_master',
      'master_consensus_update',
      'master_consensus_update_latency',
      'master_multiraft_consensus_update',
      'master_multiraft_consensus_update_latency',
      'master_table_ops',
      'master_cpu_util_secs',
      'master_yb_rpc_connections',
      'master_leaderless_and_underreplicated_tablets',
      'master_max_follower_lag',
      'master_load_balancer_stats'
    ]
  },
  master_advanced: {
    title: 'Master Server Advanced',
    metrics: [
      'master_threads_running',
      'master_log_latency',
      'master_log_bytes_written',
      'master_log_bytes_read',
      'master_tc_malloc_stats',
      'master_glog_info_messages',
      'master_lsm_rocksdb_seek_next_prev',
      'master_lsm_rocksdb_num_seeks_per_node',
      'master_lsm_rocksdb_total_sst_per_node',
      'master_lsm_rocksdb_avg_num_sst_per_node',
      'master_lsm_rocksdb_block_cache_hit_miss',
      'master_lsm_rocksdb_block_cache_usage',
      'master_lsm_rocksdb_blooms_checked_and_useful',
      'master_lsm_rocksdb_flush_size',
      'master_lsm_rocksdb_compaction',
      'master_lsm_rocksdb_compaction_numfiles',
      'master_lsm_rocksdb_compaction_time'
    ]
  },
  lsmdb: {
    title: 'DocDB',
    metrics: [
      'lsm_rocksdb_seek_next_prev',
      'lsm_rocksdb_total_sst_per_node',
      'lsm_rocksdb_avg_num_sst_per_node',
      'lsm_rocksdb_latencies_get',
      'lsm_rocksdb_latencies_write',
      'lsm_rocksdb_latencies_seek',
      'lsm_rocksdb_latencies_mutex',
      'lsm_rocksdb_block_cache_hit_miss',
      'lsm_rocksdb_block_cache_usage',
      'lsm_rocksdb_blooms_checked_and_useful',
      'lsm_rocksdb_stalls',
      'lsm_rocksdb_write_rejections',
      'lsm_rocksdb_memory_rejections',
      'lsm_rocksdb_flush_size',
      'lsm_rocksdb_compaction',
      'lsm_rocksdb_compaction_tasks',
      'lsm_rocksdb_compaction_time',
      'lsm_rocksdb_compaction_numfiles',
      'lsm_rocksdb_mem_tracker_db_memtable',
      'docdb_transaction',
      'docdb_transaction_pool_cache',
      'tablet_splitting_stats',
      'automatic_split_manager_time'
    ]
  },
  container: {
    title: 'Container',
    metrics: [
      'container_cpu_usage',
      'container_memory_usage',
      'container_volume_stats',
      'container_volume_usage_percent'
    ]
  },
  ysql_table: {
    title: 'YSQL',
    metrics: ['ysql_catalog_cache_misses']
  },
  tserver_table: {
    title: 'Tablet Server',
    metrics: [
      'table_read_latency',
      'table_read_rps',
      'table_write_latency',
      'table_write_rps',
      'tserver_log_latency',
      'tserver_log_bytes_written',
      'tserver_log_bytes_read',
      'tserver_log_ops_second',
      'tserver_log_stats',
      'tserver_write_lock_latency',
      'tserver_cache_reader_num_ops'
    ]
  },
  lsmdb_table: {
    title: 'DocDB',
    metrics: [
      'lsm_rocksdb_seek_next_prev',
      'lsm_rocksdb_total_sst_per_node',
      'lsm_rocksdb_avg_num_sst_per_node',
      'lsm_rocksdb_latencies_get',
      'lsm_rocksdb_latencies_write',
      'table_write_rejections',
      'table_memory_rejections',
      'lsm_rocksdb_latencies_seek',
      'lsm_rocksdb_block_cache_hit_miss',
      'lsm_rocksdb_blooms_checked_and_useful',
      'lsm_rocksdb_stalls',
      'lsm_rocksdb_flush_size',
      'lsm_rocksdb_compaction',
      'lsm_rocksdb_compaction_time',
      'lsm_rocksdb_compaction_numfiles',
      'docdb_transaction'
    ]
  },
  outlier_tables: {
    title: 'Outlier Tables',
    metrics: [
      'table_rocksdb_total_sst_per_node',
      'table_wal_size',
      'table_read_latency',
      'table_read_rps',
      'table_write_latency',
      'table_write_rps',
      'table_log_latency',
      'table_log_ops_second',
      'table_log_bytes_written',
      'table_write_lock_latency',
      'table_rocksdb_latencies_seek',
      'table_seek_next_prev',
      'table_ops_in_flight',
      'table_write_rejections',
      'table_memory_rejections',
      'table_compaction',
      'table_rocksdb_compaction_time',
      'table_rocksdb_compaction_numfiles',
      'table_rocksdb_stalls',
      'table_rocksdb_flush_size',
      'table_docdb_transaction',
      'table_block_cache_hit_miss',
      'table_rocksdb_blooms_checked_and_useful',
      'table_mem_tracker_db_memtable',
      'ysql_catalog_cache_misses'
    ]
  },
  outlier_databases: {
    title: 'Outlier Databases',
    metrics: [
      'table_read_latency',
      'table_read_rps',
      'table_write_latency',
      'table_write_rps',
      'table_log_latency',
      'table_log_ops_second',
      'table_log_bytes_written',
      'table_write_lock_latency',
      'table_rocksdb_latencies_seek',
      'table_seek_next_prev',
      'table_ops_in_flight',
      'table_write_rejections',
      'table_memory_rejections',
      'table_compaction',
      'table_rocksdb_compaction_time',
      'table_rocksdb_compaction_numfiles',
      'table_rocksdb_stalls',
      'table_rocksdb_flush_size',
      'table_docdb_transaction',
      'table_rocksdb_total_sst_per_node',
      'table_block_cache_hit_miss',
      'table_rocksdb_blooms_checked_and_useful',
      'table_mem_tracker_db_memtable',
      // TODO: add after https://github.com/yugabyte/yugabyte-db/issues/29491 'ysql_catalog_cache_misses'
    ]
  }
} as const;

export const MetricTypesByOrigin = {
  universe: {
    data: [
      'ysql_ops',
      'ycql_ops',
      'disk_io',
      'per_process',
      'container',
      'server',
      'tserver',
      'master',
      'master_advanced',
      'lsmdb',
      'outlier_tables',
      'outlier_databases'
    ]
  },
  customer: {
    data: [
      'ysql_ops',
      'ycql_ops',
      'disk_io',
      'container',
      'server',
      'tserver',
      'master',
      'master_advanced',
      'lsmdb',
      'outlier_tables',
      'outlier_databases'
    ]
  },
  table: {
    data: ['tserver_table', 'lsmdb_table', 'ysql_table']
  }
} as const;

export const APITypeToNodeFlags = {
  YSQL: 'isYsqlServer',
  YCQL: 'isYqlServer'
} as const;

export const APIMetricToNodeFlag = {
  ysql_ops: APITypeToNodeFlags.YSQL,
  ycql_ops: APITypeToNodeFlags.YCQL,
  sql: APITypeToNodeFlags.YSQL,
  cql: APITypeToNodeFlags.YCQL
} as const;

export const MetricConsts = {
  NODE_AVERAGE: 'Selected Nodes Average',
  ALL: 'all',
  TOP: 'top',
  PRIMARY: 'PRIMARY'
} as const;

export const NodeType = {
  ALL: 'All',
  MASTER: 'Master',
  TSERVER: 'TServer'
} as const;

export enum MetricMeasure {
  OVERALL = 'Overall',
  OUTLIER = 'Outlier',
  OUTLIER_TABLES = 'Outlier_Tables',
  OUTLIER_DATABASES = 'Outlier_Databases'
}

export const DEFAULT_OUTLIER_NUM_NODES = 3;
export const MIN_OUTLIER_NUM_NODES = 1;
export const MAX_OUTLIER_NUM_NODES = 5;
export const MAX_OUTLIER_NUM_TABLES = 7;
