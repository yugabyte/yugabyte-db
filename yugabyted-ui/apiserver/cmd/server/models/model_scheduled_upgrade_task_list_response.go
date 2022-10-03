package models

type ScheduledUpgradeTaskListResponse struct {

	Data []ScheduledUpgradeTaskData `json:"data"`

	Metadata PagingMetadata `json:"_metadata"`
}
