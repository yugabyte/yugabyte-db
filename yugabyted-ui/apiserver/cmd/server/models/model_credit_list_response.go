package models

type CreditListResponse struct {

	Data []CreditData `json:"data"`

	Metadata PagingMetadata `json:"_metadata"`
}
