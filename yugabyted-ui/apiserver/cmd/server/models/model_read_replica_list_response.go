package models

type ReadReplicaListResponse struct {

	Data []ReadReplicaData `json:"data"`

	Metadata PagingMetadata `json:"_metadata"`
}
