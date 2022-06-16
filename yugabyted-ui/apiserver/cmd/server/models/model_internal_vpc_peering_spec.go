package models

// InternalVpcPeeringSpec - Peer two yugabyte VPC
type InternalVpcPeeringSpec struct {

	VpcIdToPeer string `json:"vpcIdToPeer"`
}
