package models

// InternalClusterData - Cluster data with internal cluster details
type InternalClusterData struct {

	Spec ClusterSpec `json:"spec"`

	Info ClusterDataInfo `json:"info"`

	InternalInfo InternalClusterDataInternalInfo `json:"internal_info"`
}
