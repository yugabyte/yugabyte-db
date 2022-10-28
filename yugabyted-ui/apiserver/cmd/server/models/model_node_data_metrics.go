package models

type NodeDataMetrics struct {

    MemoryUsedBytes int64 `json:"memory_used_bytes"`

    TotalSstFileSizeBytes *int64 `json:"total_sst_file_size_bytes"`

    UncompressedSstFileSizeBytes *int64 `json:"uncompressed_sst_file_size_bytes"`

    ReadOpsPerSec float64 `json:"read_ops_per_sec"`

    WriteOpsPerSec float64 `json:"write_ops_per_sec"`
}
