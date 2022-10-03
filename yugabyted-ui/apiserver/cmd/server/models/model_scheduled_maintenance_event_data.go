package models

// ScheduledMaintenanceEventData - Scheduled Maintenance Event Data
type ScheduledMaintenanceEventData struct {

	// Scheduled Execution ID
	ScheduledExecutionId string `json:"scheduled_execution_id"`

	// indicates if this event can be delayed
	CanDelay bool `json:"can_delay"`

	// indicates if this event can be triggered immediately
	CanTrigger bool `json:"can_trigger"`

	EventType MaintenanceWindowEventTypeEnum `json:"event_type"`

	EventInfo DbUpgradeEventInfo `json:"event_info"`

	State MaintenanceWindowEventStateEnum `json:"state"`

	MaintenanceWindow MaintenanceWindowInfo `json:"maintenance_window"`
}
