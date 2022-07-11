package models

// TaskInfo - Task data
type TaskInfo struct {

	// The UUID of the task
	Id string `json:"id"`

	// The ID of the entity being operated on
	EntityId string `json:"entity_id"`

	// If the task locks the entity
	Locking bool `json:"locking"`

	EntityType EntityTypeEnum `json:"entity_type"`

	TaskType TaskTypeEnum `json:"task_type"`

	// Time of task completion
	CompletedOn *string `json:"completed_on"`

	// State of the task
	State string `json:"state"`

	Metadata EntityMetadata `json:"metadata"`
}
