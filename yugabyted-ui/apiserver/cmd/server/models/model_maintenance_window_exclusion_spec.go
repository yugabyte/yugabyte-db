package models

// MaintenanceWindowExclusionSpec - Maintenance Window Exclusion Spec
type MaintenanceWindowExclusionSpec struct {

	// start date-time for Maintenance Window Exclusion
	StartTimestamp string `json:"start_timestamp"`

	// end date-time for Maintenance Window Exclusion
	EndTimestamp string `json:"end_timestamp"`
}
