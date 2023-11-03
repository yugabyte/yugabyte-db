package models

// ClusterData - Cluster data
type ClusterData struct {

    Spec ClusterSpec `json:"spec"`

    Info ClusterDataInfo `json:"info"`
}
