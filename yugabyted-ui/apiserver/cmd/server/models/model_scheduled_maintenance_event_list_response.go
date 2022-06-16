package models

type ScheduledMaintenanceEventListResponse struct {

	Data []ScheduledMaintenanceEventData `json:"data"`

	Metadata PagingMetadata `json:"_metadata"`
}
