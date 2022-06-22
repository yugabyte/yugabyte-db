package models

// AddNetworkRequest - Payload to define a network
type AddNetworkRequest struct {

	VpcData VpcData `json:"vpc_data"`

	Subnets []SubnetData `json:"subnets"`

	// The platform with which this VPC is peered
	PlatformId string `json:"platform_id"`
}
