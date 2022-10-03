package models

type NetworkListResponse struct {

	Data []NetworkDataResponse `json:"data"`

	Metadata PagingMetadata `json:"_metadata"`
}
