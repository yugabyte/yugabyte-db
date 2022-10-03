package models

type ScheduleListResponse struct {

	Data []ScheduleData `json:"data"`

	Metadata PagingMetadata `json:"_metadata"`
}
