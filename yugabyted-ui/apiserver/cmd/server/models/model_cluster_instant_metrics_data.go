package models

// ClusterInstantMetricsData - Cluster instance metrics data
type ClusterInstantMetricsData struct {

	ReadOpsPerSecond float64 `json:"read_ops_per_second"`

	WriteOpsPerSecond float64 `json:"write_ops_per_second"`
}
