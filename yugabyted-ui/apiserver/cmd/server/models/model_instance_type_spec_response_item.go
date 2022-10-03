package models

// InstanceTypeSpecResponseItem - A HW spec for a specific VM instance type
type InstanceTypeSpecResponseItem struct {

	// Maxmium available RAM in MB
	MemoryMb int32 `json:"memory_mb"`

	// Maximum number of cores supported
	NumCores int32 `json:"num_cores"`

	// Maximum disk size in GB included for free
	IncludedDiskSizeGb int32 `json:"included_disk_size_gb"`

	// Number of AZs supported by the instance type for the given region (if applicable)
	NumAzs *int32 `json:"num_azs"`

	// Whether the instance type is enabled
	IsEnabled bool `json:"is_enabled"`
}
