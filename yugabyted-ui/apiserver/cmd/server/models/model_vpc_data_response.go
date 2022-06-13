package models

// VpcDataResponse - Information about the VPC
type VpcDataResponse struct {

	// The ID of the VPC
	Id string `json:"id"`

	VpcData VpcData `json:"vpc_data"`

	NumClusters int32 `json:"num_clusters"`
}
