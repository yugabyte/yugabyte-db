package models

// TargetClusterSizeRecommendations - schema for target cluster cpu, memory
type TargetClusterSizeRecommendations struct {

    NumNodes int32 `json:"num_nodes"`

    VcpuPerNode int32 `json:"vcpu_per_node"`

    MemoryPerNode int32 `json:"memory_per_node"`

    ConnectionsPerNode int32 `json:"connections_per_node"`

    InsertsPerNode int32 `json:"inserts_per_node"`
}
