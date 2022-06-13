package models

type AccountListResponse struct {

	Data []AccountData `json:"data"`

	Metadata PagingMetadata `json:"_metadata"`
}
