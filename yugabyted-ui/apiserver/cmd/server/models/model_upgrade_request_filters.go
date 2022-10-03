package models

// UpgradeRequestFilters - Filters to limit scheduled upgrade scope
type UpgradeRequestFilters struct {

	CloudType CloudEnum `json:"cloud_type"`

	Region *string `json:"region"`

	ClusterTier ClusterTier `json:"cluster_tier"`
}
