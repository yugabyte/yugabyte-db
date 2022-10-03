package models

// PrivateClusterInfo - Private cluster level information
type PrivateClusterInfo struct {

	ClusterTier ClusterTier `json:"cluster_tier"`

	ClusterType ClusterType `json:"cluster_type"`

	FaultTolerance ClusterFaultTolerance `json:"fault_tolerance"`

	NodeInfo ClusterNodeInfo `json:"node_info"`

	// Describes if the cluster is a production cluster
	IsProduction bool `json:"is_production"`

	// cluster data version
	Version int32 `json:"version"`
}
