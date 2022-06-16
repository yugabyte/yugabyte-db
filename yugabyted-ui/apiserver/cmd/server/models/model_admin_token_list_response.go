package models

type AdminTokenListResponse struct {

	Data []AdminTokenInfo `json:"data"`

	Metadata PagingMetadata `json:"_metadata"`
}
