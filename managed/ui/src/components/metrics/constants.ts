export const MetricOrigin = {
  TABLE: 'table',
  CUSTOMER: 'customer',
  UNIVERSE: 'universe'
} as const;

export const MetricTypes = {
  YSQL_OPS: 'ysql_ops',
  YCQL_OPS: 'ycql_ops',
  YEDIS_OPS: 'yedis_ops',
  SERVER: 'server',
  DISK_IO: 'disk_io',
  TSERVER: 'tserver',
  MASTER: 'master',
  MASTER_ADVANCED: 'master_advanced',
  LSMDB: 'lsmdb',
  CONTAINER: 'container',
  TSERVER_TABLE: 'tserver_table',
  LSMDB_TABLE: 'lsmdb_table',
  OUTLIER_TABLES: 'outlier_tables'
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
      'ysql_sql_advanced_latency'
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
  yedis_ops: {
    title: 'YEDIS',
    metrics: [
      'redis_rpcs_per_sec_all',
      'redis_ops_latency_all',
      'redis_server_rpc_p99',
      'redis_yb_local_vs_remote_ops',
      'tserver_rpc_queue_size_redis',
      'redis_yb_local_vs_remote_latency',
      'redis_reactor_latency',
      'redis_rpcs_per_sec_hash',
      'redis_ops_latency_hash',
      'redis_rpcs_per_sec_ts',
      'redis_ops_latency_ts',
      'redis_rpcs_per_sec_set',
      'redis_ops_latency_set',
      'redis_rpcs_per_sec_sortedset',
      'redis_ops_latency_sorted_set',
      'redis_rpcs_per_sec_str',
      'redis_ops_latency_str',
      'redis_rpcs_per_sec_local',
      'redis_ops_latency_local',
      'redis_yb_rpc_connections'
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
      'tserver_log_bytes_written',
    ]
  },
  tserver: {
    title: 'Tablet Server',
    metrics: [
      'tserver_rpcs_per_sec_per_node',
      'tserver_ops_latency',
      'tserver_handler_latency',
      'tserver_threads_running',
      'tserver_threads_started',
      'tserver_uptime_min',
      'tserver_consensus_rpcs_per_sec',
      'tserver_change_config',
      'tserver_remote_bootstraps',
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
  tserver_table: {
    title: 'Tablet Server',
    metrics: [
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
      'table_read_latency',
      'table_read_rps',
      'table_write_latency',
      'table_write_rps',
      'table_log_latency',
      'table_log_ops_second',
      'table_log_bytes_written',
      'table_write_lock_latency',
      'table_seek_next_prev',
      'table_ops_in_flight',
      'table_write_rejections',
      'table_memory_rejections',
      'table_compaction',
      'table_block_cache_hit_miss',
      'table_mem_tracker_db_memtable'
    ]
  }
} as const;

export const MetricTypesByOrigin = {
  universe: {
    data: [
      'ysql_ops',
      'ycql_ops',
      'yedis_ops',
      'disk_io',
      'container',
      'server',
      'tserver',
      'master',
      'master_advanced',
      'lsmdb',
      'outlier_tables'
    ]
  },
  customer: {
    data: [
      'ysql_ops',
      'ycql_ops',
      'yedis_ops',
      'disk_io',
      'container',
      'server',
      'tserver',
      'master',
      'master_advanced',
      'lsmdb',
      'outlier_tables'
    ]
  },
  table: {
    data: ['lsmdb_table', 'tserver_table']
  }
} as const;

export const APITypeToNodeFlags = {
  YSQL: 'isYsqlServer',
  YCQL: 'isYqlServer',
  YEDIS: 'isRedisServer'
} as const;

export const APIMetricToNodeFlag = {
  ysql_ops: APITypeToNodeFlags.YSQL,
  ycql_ops: APITypeToNodeFlags.YCQL,
  yedis_ops: APITypeToNodeFlags.YEDIS,
  sql: APITypeToNodeFlags.YSQL,
  cql: APITypeToNodeFlags.YCQL,
  redis: APITypeToNodeFlags.YEDIS
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
  OUTLIER_TABLES = 'Outlier_Tables'
}

export const DEFAULT_OUTLIER_NUM_NODES = 3;
export const MIN_OUTLIER_NUM_NODES = 1;
export const MAX_OUTLIER_NUM_NODES = 5;
export const MAX_OUTLIER_NUM_TABLES = 7;
