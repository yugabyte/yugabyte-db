package models
// RestoreStateEnum : State of the backup
type RestoreStateEnum string

// List of RestoreStateEnum
const (
	RESTORESTATEENUM_SUCCEEDED RestoreStateEnum = "SUCCEEDED"
	RESTORESTATEENUM_FAILED RestoreStateEnum = "FAILED"
	RESTORESTATEENUM_IN_PROGRESS RestoreStateEnum = "IN_PROGRESS"
)
