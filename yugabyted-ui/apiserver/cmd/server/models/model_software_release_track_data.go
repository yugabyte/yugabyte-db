package models

// SoftwareReleaseTrackData - Software release track data
type SoftwareReleaseTrackData struct {

	Spec SoftwareReleaseTrackSpec `json:"spec"`

	Info SoftwareReleaseTrackInfo `json:"info"`
}
