package models

// MaintenanceWindowExclusionData - Maintenance Window Exclusion Data
type MaintenanceWindowExclusionData struct {

	Info MaintenanceWindowExclusionInfo `json:"info"`

	Spec MaintenanceWindowExclusionSpec `json:"spec"`
}
