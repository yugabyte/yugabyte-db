package models

type PlacementInfo struct {

    CloudInfo CloudInfo `json:"cloud_info"`

    // How many nodes are in the region
    NumNodes int32 `json:"num_nodes"`
}
