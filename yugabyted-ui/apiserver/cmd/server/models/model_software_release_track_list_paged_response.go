package models

type SoftwareReleaseTrackListPagedResponse struct {

	Data []SoftwareReleaseTrackData `json:"data"`

	Metadata PagingMetadata `json:"_metadata"`
}
