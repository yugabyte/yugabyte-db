package models

type TaskListResponse struct {

	Data []TaskData `json:"data"`

	Metadata PagingMetadata `json:"_metadata"`
}
