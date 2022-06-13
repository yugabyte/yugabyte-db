package models

type ProjectListResponse struct {

	Data []ProjectData `json:"data"`

	Metadata PagingMetadata `json:"_metadata"`
}
