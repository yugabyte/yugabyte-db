package models

// ScheduleInfo - Schedule info
type ScheduleInfo struct {

	// Id of the schedule
	Id string `json:"id"`

	TaskType TaskTypeEnum `json:"task_type"`

	// Task params for the schedule type
	TaskParams map[string]interface{} `json:"task_params"`

	EntityType EntityTypeEnum `json:"entity_type"`

	// Entity id for the schedule
	EntityId string `json:"entity_id"`

	ScheduleType ScheduleTypeEnum `json:"schedule_type"`

	// Timestamp when the last schedule instance ran in UTC
	LastScheduleTime *string `json:"last_schedule_time"`

	// Timestamp when the next schedule instance will run in UTC
	NextScheduleTime *string `json:"next_schedule_time"`
}
