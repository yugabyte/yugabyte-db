// The JSON pages' shapes. Those also served by the C++ master are an external contract (YBA and
// scripts parse them); master/master-path-handlers.cc is their source of truth. Fields the UI does
// not need are omitted, and every field is treated as possibly missing.

export interface PathMetrics {
  path: string;
  space_used: number;
  total_space_size: number;
  storage_tier?: string;
}

export interface TServer {
  time_since_hb?: string;
  time_since_hb_sec?: number;
  status?: string;
  uptime_seconds?: number;
  has_faulty_drive?: boolean;
  start_time_us?: number;
  ram_used?: string;
  ram_used_bytes?: number;
  num_sst_files?: number;
  total_sst_file_size?: string;
  total_sst_file_size_bytes?: number;
  uncompressed_sst_file_size?: string;
  uncompressed_sst_file_size_bytes?: number;
  path_metrics?: PathMetrics[];
  read_ops_per_sec?: number;
  write_ops_per_sec?: number;
  user_tablets_total?: number;
  user_tablets_leaders?: number;
  system_tablets_total?: number;
  system_tablets_leaders?: number;
  active_tablets?: number;
  cloud?: string;
  region?: string;
  zone?: string;
  permanent_uuid?: string;
  lease_info?: { is_live?: boolean; lease_expiry_sec?: number; lease_epoch?: number };
}

// placement uuid -> "host:http_port" -> tserver.
export type TServersJson = Record<string, Record<string, TServer>>;

export interface HealthCheck {
  dead_nodes?: string[];
  most_recent_uptime?: number;
  under_replicated_tablets?: string[];
}

export interface OnDiskSize {
  wal_files_size_bytes?: number;
  sst_files_size_bytes?: number;
  uncompressed_sst_file_size_bytes?: number;
  vector_index_size_bytes?: number;
  has_missing_size?: boolean;
}

export interface TableRow {
  keyspace?: string;
  table_name?: string;
  state?: string;
  message?: string;
  uuid: string;
  ysql_oid?: string | number;
  hidden?: boolean;
  parent_oid?: string | number;
  colocation_id?: string | number;
  on_disk_size?: OnDiskSize;
}

export interface TablesJson {
  user?: TableRow[];
  index?: TableRow[];
  parent?: TableRow[];
  system?: TableRow[];
}

export interface TabletLocation {
  uuid: string;
  role: string;
  location: string;
}

export interface TabletRow {
  tablet_id: string;
  partition?: string;
  split_depth?: number;
  state?: string;
  hidden?: string;
  message?: string;
  locations?: TabletLocation[];
}

export interface TaskRow {
  task_type?: string;
  task_state?: string;
  task_time_since_started?: string;
  task_running_secs?: string;
  task_description?: string;
}

export interface TableJson {
  error?: string;
  table_name?: string;
  table_id?: string;
  table_version?: number;
  table_type?: string;
  table_state?: string;
  table_state_message?: string;
  table_tablespace_oid?: number;
  table_replication_info?: unknown;
  table_transaction_verifier_state?: string;
  table_transaction_ddl?: string;
  columns?: { column: string; id: string; type: string }[];
  tablets?: TabletRow[];
  Tasks?: TaskRow[];
}

export interface NamespaceRow {
  name?: string;
  id: string;
  language?: string;
  state?: string;
  colocated?: string | boolean;
}

export type NamespacesJson = Record<string, NamespaceRow[]>;

export interface LeaderlessJson {
  leaderless_tablets?: { table_uuid?: string; tablet_uuid: string; reason?: string }[];
}

export interface UnderReplicatedJson {
  underreplicated_tablets?: {
    table_uuid?: string;
    tablet_uuid: string;
    expected_num_replicas?: number;
    underreplicated_placements?: string[];
    missing_replicas?: {
      placement_uuid?: string;
      is_live?: boolean;
      missing_replicas?: number;
      placement_blocks?: { cloud_info?: CloudInfo; missing_replicas?: number }[];
    }[];
  }[];
}

export interface CloudInfo {
  placement_cloud?: string;
  placement_region?: string;
  placement_zone?: string;
}

export interface HostPort {
  host: string;
  port: number;
}

export interface MasterEntry {
  error?: { code?: string; message?: string };
  instance_id?: { permanent_uuid?: string; instance_seqno?: number; start_time_us?: number };
  registration?: {
    private_rpc_addresses?: HostPort[];
    broadcast_addresses?: HostPort[];
    http_addresses?: HostPort[];
    cloud_info?: CloudInfo;
    placement_uuid?: string;
  };
  role?: string;
  heartbeat_delay_ms?: number;
}

export interface MastersJson {
  masters?: MasterEntry[];
  error?: unknown;
}

export interface VersionJson {
  version_number?: string;
  build_number?: string;
  git_hash?: string;
  build_type?: string;
  build_timestamp?: string;
  build_hostname?: string;
  build_username?: string;
  build_clean_repo?: boolean;
  build_id?: string;
  ysql_major_version?: number;
}

export interface PlacementSummary {
  placement_uuid: string;
  is_primary?: boolean;
  live_tservers?: number;
  blacklisted_tservers?: number;
  dead_tservers?: number;
  user_tablet_peers?: number;
  system_tablet_peers?: number;
  hidden_tablet_peers?: number;
  active_tablet_peers?: number;
  tablet_peer_limit?: number | null;
}

export interface OverviewJson {
  universe_uuid?: string;
  cluster_uuid?: string;
  cluster_config_version?: number;
  placements?: PlacementSummary[];
  replication_factor?: number;
  live_tservers?: number;
  dead_tservers?: number;
  load_balancer_enabled?: boolean;
  is_load_balanced?: boolean | null;
  encryption_at_rest?: string;
  tablet_limits?: { tablet_peer_limit?: number | null; tablet_peers?: number; enforced?: boolean };
}

export interface LoadDistributionJson {
  ongoing_remote_bootstraps?: {
    tablet_id: string;
    table?: string;
    source_ts_uuid?: string;
    dest_ts_uuid?: string;
    progress?: string | number;
  }[];
  warnings_summary?: { example_message?: string; count?: number }[];
  tasks_summary?: {
    example_description?: string;
    state?: string;
    count?: number;
    example_status?: string;
  }[];
  tablet_distribution?: {
    tservers?: {
      uuid: string;
      host_port?: string;
      cloud?: string;
      region?: string;
      zone?: string;
      placement_uuid?: string;
      alive?: boolean;
    }[];
    tables?: {
      table_id: string;
      keyspace?: string;
      table_name?: string;
      counts?: { uuid: string; replicas?: number; leaders?: number }[];
    }[];
  };
}

export interface TasksJson {
  running?: { name?: string; count?: number }[];
}

export interface ClocksJson {
  tservers?: {
    uuid: string;
    host_port?: string;
    time_since_hb_sec?: number;
    physical_time_us?: number;
    hybrid_time?: string;
    ysql_lease?: YsqlLease;
  }[];
}

export interface YsqlLease {
  state?: 'N/A' | 'NO LEASE' | 'RELINQUISHED' | 'EXPIRED' | 'LIVE' | string;
  epoch?: number;
  // To the deadline when LIVE, since it when EXPIRED.
  seconds?: number;
}

export interface StatefulServicesJson {
  stateful_services?: {
    service_name?: string;
    hosting_server?: string;
    table_id?: string;
    tablet_id?: string;
  }[];
}

export interface XClusterJson {
  error?: string;
  xcluster_config?: unknown;
  outbound_replication_groups?: { replication_group_id?: string; metadata?: unknown }[];
  outbound_streams?: { stream_id?: string; metadata?: unknown }[];
  replication_status?: unknown;
  replication_infos?: unknown[];
  safe_time_map?: { consumer_namespace_id?: string; safe_time?: string | number }[];
  consumer_registry?: unknown;
}
