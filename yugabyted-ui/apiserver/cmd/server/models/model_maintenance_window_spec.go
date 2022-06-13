package models

// MaintenanceWindowSpec - Maintenance Window Spec
type MaintenanceWindowSpec struct {

	Day DayEnum `json:"day"`

	// Start time for Maintenance Windows
	Hour int32 `json:"hour"`
}
