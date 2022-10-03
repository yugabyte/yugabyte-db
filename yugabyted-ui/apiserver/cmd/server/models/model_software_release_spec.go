package models

// SoftwareReleaseSpec - DB software release
type SoftwareReleaseSpec struct {

	Version string `json:"version"`

	ReleaseDate string `json:"release_date"`

	IsDefault bool `json:"is_default"`
}
