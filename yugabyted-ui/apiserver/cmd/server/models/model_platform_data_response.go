package models

// PlatformDataResponse - Information about YB Platform
type PlatformDataResponse struct {

	// The UUID of the platform
	Id string `json:"id"`

	PlatformData PlatformData `json:"platform_data"`

	NumClusters int32 `json:"num_clusters"`
}
