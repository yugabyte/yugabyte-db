package models

// ClusterTablespace - Model representing a tablespace
type ClusterTablespace struct {

	Name string `json:"name"`

	NumReplicas int32 `json:"num_replicas"`

	PlacementInfoList []TablePlacementInfo `json:"placement_info_list"`
}
