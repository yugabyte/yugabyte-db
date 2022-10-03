package models

// MaintenanceScheduleData - Maintenance Schedule Data
type MaintenanceScheduleData struct {

	Info MaintenanceScheduleInfo `json:"info"`

	Spec MaintenanceScheduleSpec `json:"spec"`
}
