package models

// TablePlacementInfo - Model representing a table placement
type TablePlacementInfo struct {

	Cloud CloudEnum `json:"cloud"`

	Region string `json:"region"`

	Zone string `json:"zone"`

	MinNumReplicas int32 `json:"min_num_replicas"`
}
