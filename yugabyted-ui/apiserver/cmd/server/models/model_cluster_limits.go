package models

// ClusterLimits - Specifications for cluster limits
type ClusterLimits struct {

	NumCores int32 `json:"num_cores"`

	MemoryMb int32 `json:"memory_mb"`

	DiskSizeGb int32 `json:"disk_size_gb"`
}
