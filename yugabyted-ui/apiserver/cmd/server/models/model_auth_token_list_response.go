package models

type AuthTokenListResponse struct {

	Data []AuthTokenData `json:"data"`

	Metadata PagingMetadata `json:"_metadata"`
}
