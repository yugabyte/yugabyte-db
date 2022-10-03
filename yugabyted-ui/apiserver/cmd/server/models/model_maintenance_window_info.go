package models

// MaintenanceWindowInfo - Maintenance Window Info
type MaintenanceWindowInfo struct {

	// Start time of upgrade in UTC
	StartTimestamp string `json:"start_timestamp"`

	// End time of upgrade in UTC
	EndTimestamp string `json:"end_timestamp"`
}
