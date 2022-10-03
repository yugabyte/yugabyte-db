package models

// AlertNotificationInfo - Alert notification info
type AlertNotificationInfo struct {

	// Unique id of the record
	Id string `json:"id"`

	// Alert notification name
	Name string `json:"name"`

	// Alert notification description
	Description string `json:"description"`

	// Alert notification source id i.e cluster id or account id (In case of billing)
	SourceId string `json:"source_id"`

	// Name of the cluster if source type is CLUSTER
	SourceName *string `json:"source_name"`

	Severity AlertRuleSeverityEnum `json:"severity"`

	// Alert notification trigered time
	TriggerTime string `json:"trigger_time"`
}
