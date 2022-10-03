package models

type SingleTenantVpcDataResponse struct {

	Spec SingleTenantVpcSpec `json:"spec"`

	Info SingleTenantVpcDataResponseInfo `json:"info"`
}
