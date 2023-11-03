package models

type ClusterTabletListResponse struct {

    // List of cluster tablets
    Data map[string]ClusterTablet `json:"data"`
}
