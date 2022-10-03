package models

// ScheduleSpec - Schedule spec
type ScheduleSpec struct {

	// Time interval between two scheduled tasks in days
	TimeIntervalInDays int32 `json:"time_interval_in_days"`

	// Cron expression of the schedule
	CronExpression *string `json:"cron_expression"`

	State ScheduleStateEnum `json:"state"`
}
