package models

type ClusterListResponse struct {

	Data []ClusterData `json:"data"`

	Metadata PagingMetadata `json:"_metadata"`
}
