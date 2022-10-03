package models

type ListAuditEventResponse struct {

	Data []AuditEventData `json:"data"`

	Metadata PagingMetadata `json:"_metadata"`
}
