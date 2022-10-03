package models

// SoftwareReleaseTrackInfo - Software release track Info
type SoftwareReleaseTrackInfo struct {

	// The UUID of the release track
	Id string `json:"id"`

	DefaultReleaseId *string `json:"default_release_id"`
}
