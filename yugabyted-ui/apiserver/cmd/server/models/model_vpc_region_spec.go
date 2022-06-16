package models

type VpcRegionSpec struct {

	Region string `json:"region"`

	// A CIDR block
	Cidr string `json:"cidr"`
}
