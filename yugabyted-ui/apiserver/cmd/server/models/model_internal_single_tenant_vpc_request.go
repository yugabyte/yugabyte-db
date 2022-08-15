package models

type InternalSingleTenantVpcRequest struct {

	Spec SingleTenantVpcSpec `json:"spec"`

	VpcId string `json:"vpc_id"`

	ParentId *string `json:"parent_id"`
}
