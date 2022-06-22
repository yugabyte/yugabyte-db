package models

// ClusterNodeInfo - Node level information
type ClusterNodeInfo struct {

	// The number of cores for each node
	NumCores int32 `json:"num_cores"`

	// The amount of RAM (MB) for each node
	MemoryMb int32 `json:"memory_mb"`

	// The size of disk (GB) of each node
	DiskSizeGb int32 `json:"disk_size_gb"`
}
