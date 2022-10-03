package models

// ClusterInstantMetrics - Cluster instant metrics
type ClusterInstantMetrics struct {

	ClusterId string `json:"cluster_id"`

	Metrics *ClusterInstantMetricsData `json:"metrics"`
}
