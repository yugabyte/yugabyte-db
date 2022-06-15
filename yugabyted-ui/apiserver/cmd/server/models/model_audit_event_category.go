package models

// AuditEventCategory - Audit Event Category
type AuditEventCategory struct {

	ResourceType ResourceTypeEnum `json:"resource_type"`

	EventTypes []EventTypeEnum `json:"event_types"`
}
