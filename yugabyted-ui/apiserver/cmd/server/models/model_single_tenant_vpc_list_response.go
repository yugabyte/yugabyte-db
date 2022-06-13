package models

type SingleTenantVpcListResponse struct {

	Data []SingleTenantVpcDataResponse `json:"data"`

	Metadata PagingMetadata `json:"_metadata"`
}
