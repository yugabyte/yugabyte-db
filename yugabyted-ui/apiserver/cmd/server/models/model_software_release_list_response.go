package models

type SoftwareReleaseListResponse struct {

	Data []SoftwareReleaseData `json:"data"`

	Metadata PagingMetadata `json:"_metadata"`
}
