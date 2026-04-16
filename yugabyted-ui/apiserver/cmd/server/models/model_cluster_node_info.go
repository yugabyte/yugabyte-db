package models

// ClusterNodeInfo - Node level information
type ClusterNodeInfo struct {

    // The total amount of RAM (MB) used by all nodes
    MemoryMb float64 `json:"memory_mb"`

    // The total size of disk (GB)
    DiskSizeGb float64 `json:"disk_size_gb"`

    // The total size of used disk space (GB)
    DiskSizeUsedGb float64 `json:"disk_size_used_gb"`

    // The average CPU usage over all nodes
    CpuUsage float64 `json:"cpu_usage"`

    // The number of CPU cores per node
    NumCores int32 `json:"num_cores"`

    // The total size of provisioned ram (GB)
    RamProvisionedGb float64 `json:"ram_provisioned_gb"`
}
