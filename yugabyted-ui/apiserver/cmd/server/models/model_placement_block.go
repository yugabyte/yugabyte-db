package models

// PlacementBlock - Placement Block
type PlacementBlock struct {

    CloudInfo PlacementCloudInfo `json:"cloud_info"`

    MinNumReplicas int32 `json:"min_num_replicas"`
}
