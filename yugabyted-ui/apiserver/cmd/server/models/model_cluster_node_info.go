package models

// ClusterNodeInfo - Node level information
type ClusterNodeInfo struct {

    // The total amount of RAM (MB) used by all nodes
    MemoryMb float64 `json:"memory_mb"`

    // The total size of disk (GB)
    DiskSizeGb int32 `json:"disk_size_gb"`

    // The average CPU usage over all nodes
    CpuUsage float64 `json:"cpu_usage"`
}
