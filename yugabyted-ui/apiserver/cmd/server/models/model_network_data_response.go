package models

// NetworkDataResponse - Information about cluster network
type NetworkDataResponse struct {

	Vpc VpcDataResponse `json:"vpc"`

	Subnets []SubnetDataResponse `json:"subnets"`
}
