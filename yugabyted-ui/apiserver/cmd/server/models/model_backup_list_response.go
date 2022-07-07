package models

type BackupListResponse struct {

	Data []BackupData `json:"data"`

	Metadata PagingMetadata `json:"_metadata"`
}
