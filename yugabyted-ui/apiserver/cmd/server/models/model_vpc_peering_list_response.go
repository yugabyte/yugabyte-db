package models

type VpcPeeringListResponse struct {

	Data []VpcPeeringData `json:"data"`

	Metadata PagingMetadata `json:"_metadata"`
}
