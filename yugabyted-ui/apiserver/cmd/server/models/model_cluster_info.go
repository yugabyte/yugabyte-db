package models

// ClusterInfo - Cluster level information
type ClusterInfo struct {

    // How many nodes are in the cluster
    NumNodes int32 `json:"num_nodes"`

    FaultTolerance ClusterFaultTolerance `json:"fault_tolerance"`

    NodeInfo ClusterNodeInfo `json:"node_info"`

    // Describes if the cluster is a production cluster
    IsProduction bool `json:"is_production"`

    // cluster data version
    Version *int32 `json:"version"`
}
