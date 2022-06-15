package models

type CustomerVpcSpec struct {

	ExternalVpcId string `json:"external_vpc_id"`

	CloudProviderProject string `json:"cloud_provider_project"`

	// A CIDR block
	Cidr string `json:"cidr"`

	CloudInfo VpcCloudInfo `json:"cloud_info"`
}
