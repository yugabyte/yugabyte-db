package models

type RestoreListResponse struct {

	Data []RestoreData `json:"data"`

	Metadata PagingMetadata `json:"_metadata"`
}
