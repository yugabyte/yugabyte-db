package models

// PausedClusterRate - Paused cluster rate info
type PausedClusterRate struct {

	// Cluster rate per hour when cluster is in paused state
	RatePerHour float64 `json:"rate_per_hour"`
}
