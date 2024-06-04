package models

type NodeDataMetrics struct {

    MemoryUsedBytes int64 `json:"memory_used_bytes"`

    TotalSstFileSizeBytes *int64 `json:"total_sst_file_size_bytes"`

    UncompressedSstFileSizeBytes *int64 `json:"uncompressed_sst_file_size_bytes"`

    ReadOpsPerSec float64 `json:"read_ops_per_sec"`

    WriteOpsPerSec float64 `json:"write_ops_per_sec"`

    TimeSinceHbSec float64 `json:"time_since_hb_sec"`

    UptimeSeconds int64 `json:"uptime_seconds"`

    UserTabletsTotal int64 `json:"user_tablets_total"`

    UserTabletsLeaders int64 `json:"user_tablets_leaders"`

    SystemTabletsTotal int64 `json:"system_tablets_total"`

    SystemTabletsLeaders int64 `json:"system_tablets_leaders"`

    ActiveConnections NodeDataMetricsActiveConnections `json:"active_connections"`

    MasterUptimeUs int64 `json:"master_uptime_us"`

    RamUsedBytes int64 `json:"ram_used_bytes"`

    RamProvisionedBytes int64 `json:"ram_provisioned_bytes"`

    DiskProvisionedBytes int64 `json:"disk_provisioned_bytes"`
}
