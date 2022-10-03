package models

// SubnetData - Information about the Subnet
type SubnetData struct {

	// The subnet reference ID in the provider cloud
	SubnetId string `json:"subnet_id"`

	// The region/zone of the subnet
	SubnetMapping string `json:"subnet_mapping"`

	// If the subnet is used for the primary NIC
	IsPrimary bool `json:"is_primary"`
}
