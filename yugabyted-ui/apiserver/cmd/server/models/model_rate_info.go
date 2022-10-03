package models

// RateInfo - Rate info
type RateInfo struct {

	ActiveCluster ActiveClusterRate `json:"active_cluster"`

	PausedCluster PausedClusterRate `json:"paused_cluster"`
}
