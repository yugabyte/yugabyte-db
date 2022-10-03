package models

type DefaultInternalTagsListResponse struct {

	Data []InternalTagsSpec `json:"data"`

	Metadata PagingMetadata `json:"_metadata"`
}
