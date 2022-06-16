package models
// PeeringStateEnum : The current state of a VPC peering connection
type PeeringStateEnum string

// List of PeeringStateEnum
const (
	PEERINGSTATEENUM_CREATING PeeringStateEnum = "CREATING"
	PEERINGSTATEENUM_FAILED PeeringStateEnum = "FAILED"
	PEERINGSTATEENUM_ACTIVE PeeringStateEnum = "ACTIVE"
	PEERINGSTATEENUM_PENDING PeeringStateEnum = "PENDING"
	PEERINGSTATEENUM_EXPIRED PeeringStateEnum = "EXPIRED"
	PEERINGSTATEENUM_DELETING PeeringStateEnum = "DELETING"
	PEERINGSTATEENUM_DELETED PeeringStateEnum = "DELETED"
)
