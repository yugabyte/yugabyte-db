package models

// ClusterMeteringEvent - Cluster metering event
type ClusterMeteringEvent struct {

	StartTimestamp string `json:"start_timestamp"`

	PeriodInMins float64 `json:"period_in_mins"`

	IsActive bool `json:"is_active"`

	ClusterData ClusterData `json:"cluster_data"`
}
