package models

type VpcPeeringSpec struct {

	InternalYugabyteVpcId string `json:"internal_yugabyte_vpc_id"`

	// The name of the peering
	Name string `json:"name"`

	CustomerVpc CustomerVpcSpec `json:"customer_vpc"`
}
