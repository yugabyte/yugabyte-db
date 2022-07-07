package models

// SoftwareReleaseInfo - Software release track Info
type SoftwareReleaseInfo struct {

	// The UUID of the release
	Id string `json:"id"`

	// The UUID of the release track
	TrackId string `json:"track_id"`
}
