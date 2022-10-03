package models

// VpcData - Information about the VPC
type VpcData struct {

	// The reference ID of the VPC in the provider cloud
	VpcId string `json:"vpc_id"`

	// The account in AWS/project in GCP that this VPC belongs to.
	CloudProject string `json:"cloud_project"`

	CloudInfo CloudInfo `json:"cloud_info"`

	// A CIDR block
	Cidr string `json:"cidr"`

	VpcType string `json:"vpc_type"`
}
