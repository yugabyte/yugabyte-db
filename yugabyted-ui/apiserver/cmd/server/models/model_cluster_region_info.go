package models

// ClusterRegionInfo - Cluster region info list
type ClusterRegionInfo struct {

	PlacementInfo PlacementInfo `json:"placement_info"`

	IsDefault bool `json:"is_default"`

	// If the leaders should be pinned to this region
	IsAffinitized *bool `json:"is_affinitized"`
}
