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
  REDIS: 'redis',
  TSERVER: 'tserver',
  MASTER: 'master',
  MASTER_ADVANCED: 'master_advanced',
  LSMDB: 'lsmdb',
  CONTAINER: 'container',
  SQL: 'sql',
  CQL: 'cql',
  TSERVER_TABLE: 'tserver_table',
  LSMDB_TABLE: 'lsmdb_table'
} as const;

export const MetricTypesWithOperations = {
  ysql_ops: {
    title: 'YSQL Ops',
    metrics: [
      'ysql_server_rpc_per_second',
      'ysql_sql_latency',
      'ysql_connections'
      // TODO(bogdan): Add these in once we have histogram support, see #3630.
      // "ysql_server_rpc_p99"
    ]
  },
  ycql_ops: {
    title: 'YCQL Ops',
    metrics: ['cql_server_rpc_per_second', 'cql_sql_latency', 'cql_server_rpc_p99']
  },
  yedis_ops: {
    title: 'YEDIS Ops',
    metrics: ['redis_rpcs_per_sec_all', 'redis_ops_latency_all', 'redis_server_rpc_p99']
  },
  server: {
    title: 'Resource',
    metrics: [
      'cpu_usage',
      'memory_usage',
      'disk_iops',
      'disk_bytes_per_second_per_node',
      'network_packets',
      'network_bytes',
      'network_errors',
      'system_load_over_time',
      'node_clock_skew'
    ]
  },
  redis: {
    title: 'YEDIS Advanced',
    metrics: [
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
  tserver: {
    title: 'Tablet Server',
    metrics: [
      'tserver_rpcs_per_sec_per_node',
      'tserver_ops_latency',
      'tserver_handler_latency',
      'tserver_threads_running',
      'tserver_threads_started',
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
      'tserver_tc_malloc_stats',
      'tserver_log_stats',
      'tserver_cache_reader_num_ops',
      'tserver_glog_info_messages',
      'tserver_rpc_queue_size_tserver',
      'tserver_cpu_util_secs',
      'tserver_yb_rpc_connections'
    ]
  },
  master: {
    title: 'Master Server',
    metrics: [
      'master_overall_rpc_rate',
      'master_latency',
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
      'master_yb_rpc_connections'
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
      'master_lsm_rocksdb_num_seek_or_next',
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
    title: 'Docs DB',
    metrics: [
      'lsm_rocksdb_num_seek_or_next',
      'lsm_rocksdb_num_seeks_per_node',
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
      'lsm_rocksdb_flush_size',
      'lsm_rocksdb_compaction',
      'lsm_rocksdb_compaction_time',
      'lsm_rocksdb_compaction_numfiles',
      'docdb_transaction',
      'docdb_transaction_pool_cache',
    ]
  },
  container: {
    title: 'Container',
    metrics: ['container_cpu_usage', 'container_memory_usage', 'container_volume_stats']
  },
  sql: {
    title: 'YSQL Advanced',
    metrics: ['ysql_server_advanced_rpc_per_second', 'ysql_sql_advanced_latency']
  },
  cql: {
    title: 'YCQL Advanced',
    metrics: [
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
  tserver_table: {
    title: 'Tablet Server',
    metrics: [
      'tserver_log_latency',
      'tserver_log_bytes_written',
      'tserver_log_bytes_read',
      'tserver_log_ops_second',
      'tserver_log_stats',
      'tserver_cache_reader_num_ops'
    ]
  },
  lsmdb_table: {
    title: 'DocDB',
    metrics: [
      'lsm_rocksdb_num_seek_or_next',
      'lsm_rocksdb_num_seeks_per_node',
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
  }
} as const;

export const MetricTypesByOrigin= {
  universe: {
    data: [
      'ysql_ops',
      'ycql_ops',
      'yedis_ops',
      'container',
      'server',
      'sql',
      'cql',
      'redis',
      'tserver',
      'master',
      'master_advanced',
      'lsmdb'
    ],
    isOpen: [true, true, false, false, false, false, false, false, false, false]
  },
  customer: {
    data: [
      'ysql_ops',
      'ycql_ops',
      'yedis_ops',
      'container',
      'server',
      'cql',
      'redis',
      'tserver',
      'master',
      'master_advanced',
      'lsmdb'
    ],
    isOpen: [true, true, false, false, false, false, false, false, false, false]
  },
  table: {
    data: ['lsmdb_table', 'tserver_table'],
    isOpen: [true, true]
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
  CLUSTER_AVERAGE: 'Cluster average',
  ALL: 'all',
  TOP: 'top',
  PRIMARY: 'PRIMARY'
} as const;

export enum MetricMeasure {
  OVERALL = 'Overall',
  OUTLIER = 'Outlier'
};

export const DEFAULT_OUTLIER_NUM_NODES = 3;
export const MIN_OUTLIER_NUM_NODES = 1;
export const MAX_OUTLIER_NUM_NODES = 5;
