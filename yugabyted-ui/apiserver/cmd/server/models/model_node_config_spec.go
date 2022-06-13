package models

// NodeConfigSpec - Node config spec
type NodeConfigSpec struct {

	NumVcpus int32 `json:"num_vcpus"`

	DiskStorageGb int32 `json:"disk_storage_gb"`
}
