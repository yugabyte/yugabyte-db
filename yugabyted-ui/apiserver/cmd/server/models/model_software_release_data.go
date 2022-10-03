package models

// SoftwareReleaseData - Software release data
type SoftwareReleaseData struct {

	Spec SoftwareReleaseSpec `json:"spec"`

	Info SoftwareReleaseInfo `json:"info"`
}
