package models

// SubnetDataResponse - Information about the subnet
type SubnetDataResponse struct {

	// The ID of the subnet
	Id string `json:"id"`

	SubnetData SubnetData `json:"subnet_data"`
}
