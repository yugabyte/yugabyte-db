package models

type PlacementInfo struct {

	CloudInfo CloudInfo `json:"cloud_info"`

	// How many nodes are in the region
	NumNodes int32 `json:"num_nodes"`

	VpcId *string `json:"vpc_id"`

	// Number of data copies (read replica only)
	NumReplicas *int32 `json:"num_replicas"`

	// Whether to spread the nodes in this region across zones
	MultiZone *bool `json:"multi_zone"`
}
