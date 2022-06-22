package models

// FreeTierSpecsData - Free tier specifications
type FreeTierSpecsData struct {

	ClusterLimits ClusterLimits `json:"cluster_limits"`

	Apis []YbApiEnum `json:"apis"`
}
