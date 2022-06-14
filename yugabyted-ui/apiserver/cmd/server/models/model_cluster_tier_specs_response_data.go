package models

// ClusterTierSpecsResponseData - Cluster specifications data
type ClusterTierSpecsResponseData struct {

	FreeTier FreeTierSpecsData `json:"free_tier"`

	PaidTier PaidTierSpecsData `json:"paid_tier"`
}
