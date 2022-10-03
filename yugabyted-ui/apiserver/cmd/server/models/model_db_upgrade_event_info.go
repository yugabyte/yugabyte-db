package models

// DbUpgradeEventInfo - DB Upgrade Event Info
type DbUpgradeEventInfo struct {

	// Release ID
	ReleaseId string `json:"release_id"`

	// Track ID
	TrackId string `json:"track_id"`

	// Upgrade Version
	Version string `json:"version"`

	// Estimated time of completion in minutes
	EstimatedTimeMins int32 `json:"estimated_time_mins"`
}
