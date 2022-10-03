package models

// ActiveClusterRate - Active cluster rate info
type ActiveClusterRate struct {

	// Cluster rate per hour when cluster is in active state
	RatePerHour float64 `json:"rate_per_hour"`
}
