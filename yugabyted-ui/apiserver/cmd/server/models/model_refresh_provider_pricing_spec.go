package models

// RefreshProviderPricingSpec - Refresh provider pricing for clusters matching these filters
type RefreshProviderPricingSpec struct {

	Cloud CloudEnum `json:"cloud"`

	Region string `json:"region"`

	Tier ClusterTier `json:"tier"`

	ClusterIds []string `json:"cluster_ids"`
}
