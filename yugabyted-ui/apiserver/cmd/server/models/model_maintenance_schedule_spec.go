package models

// MaintenanceScheduleSpec - Maintenance Schedule Spec
type MaintenanceScheduleSpec struct {

	Windows MaintenanceWindowSpec `json:"windows"`

	Exclusions []MaintenanceWindowExclusionData `json:"exclusions"`
}
