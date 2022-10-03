package models

type SingleTenantVpcSpec struct {

	// The name of the vpc
	Name string `json:"name"`

	Cloud CloudEnum `json:"cloud"`

	// A CIDR block
	ParentCidr string `json:"parent_cidr"`

	RegionSpecs []VpcRegionSpec `json:"region_specs"`
}
