package models

// ReadReplicaData - Cluster data
type ReadReplicaData struct {

	Spec ReadReplicaSpec `json:"spec"`

	Info ClusterDataInfo `json:"info"`
}
