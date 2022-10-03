package models

// ReadReplicaSpec - Read Replica Spec
type ReadReplicaSpec struct {

	NodeInfo ClusterNodeInfo `json:"node_info"`

	PlacementInfo PlacementInfo `json:"placement_info"`

	PrimaryClusterId string `json:"primary_cluster_id"`
}
