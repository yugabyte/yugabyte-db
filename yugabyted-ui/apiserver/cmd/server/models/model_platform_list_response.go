package models

type PlatformListResponse struct {

	Data []PlatformDataResponse `json:"data"`

	Metadata PagingMetadata `json:"_metadata"`
}
