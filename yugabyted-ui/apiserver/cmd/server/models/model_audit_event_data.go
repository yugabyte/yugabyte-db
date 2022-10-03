package models

// AuditEventData - Audit event data
type AuditEventData struct {

	EntityName string `json:"entity_name"`

	AuditEventId string `json:"audit_event_id"`

	AccountId string `json:"account_id"`

	AccountName *string `json:"account_name"`

	ProjectId *string `json:"project_id"`

	ProjectName *string `json:"project_name"`

	ClusterId *string `json:"cluster_id"`

	ClusterName *string `json:"cluster_name"`

	UserId *string `json:"user_id"`

	UserEmail *string `json:"user_email"`

	ImpersonatingUserEmail *string `json:"impersonating_user_email"`

	TaskId *string `json:"task_id"`

	Message string `json:"message"`

	IsSuccessful bool `json:"is_successful"`

	ResourceType string `json:"resource_type"`

	EventType string `json:"event_type"`

	CreatedOn string `json:"created_on"`
}
