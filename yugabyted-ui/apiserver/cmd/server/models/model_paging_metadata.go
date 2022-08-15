package models

// PagingMetadata - API pagination metadata
type PagingMetadata struct {

	ContinuationToken *string `json:"continuation_token"`

	Links PagingLinks `json:"links"`
}
