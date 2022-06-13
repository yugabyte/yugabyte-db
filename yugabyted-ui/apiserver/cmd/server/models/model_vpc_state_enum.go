package models
// VpcStateEnum : The current state of a VPC
type VpcStateEnum string

// List of VpcStateEnum
const (
	VPCSTATEENUM_CREATING VpcStateEnum = "CREATING"
	VPCSTATEENUM_FAILED VpcStateEnum = "FAILED"
	VPCSTATEENUM_ACTIVE VpcStateEnum = "ACTIVE"
	VPCSTATEENUM_DELETING VpcStateEnum = "DELETING"
)
