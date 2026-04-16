package models

// SourceEnvironmentInfo - Assessment source environment (node/VM) details
type SourceEnvironmentInfo struct {

    TotalVcpu string `json:"total_vcpu"`

    TotalMemory string `json:"total_memory"`

    TotalDiskSize string `json:"total_disk_size"`

    NoOfConnections string `json:"no_of_connections"`
}
