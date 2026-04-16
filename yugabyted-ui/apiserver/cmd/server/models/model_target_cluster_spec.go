package models

// TargetClusterSpec - schema for target cluster cpu, memory
type TargetClusterSpec struct {

    NumNodes int64 `json:"num_nodes"`

    VcpuPerNode int64 `json:"vcpu_per_node"`

    MemoryPerNode int64 `json:"memory_per_node"`

    ConnectionsPerNode int64 `json:"connections_per_node"`

    InsertsPerNode int64 `json:"inserts_per_node"`
}
