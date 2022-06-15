package models

type AuditEventResponse struct {

	AuditEventData AuditEventData `json:"audit_event_data"`

	// Audit Event detailed data
	AuditEventDetailedData map[string]map[string]interface{} `json:"audit_event_detailed_data"`
}
