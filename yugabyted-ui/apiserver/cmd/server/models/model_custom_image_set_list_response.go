package models

type CustomImageSetListResponse struct {

	Data []CustomImageSetListItem `json:"data"`

	Metadata PagingMetadata `json:"_metadata"`
}
