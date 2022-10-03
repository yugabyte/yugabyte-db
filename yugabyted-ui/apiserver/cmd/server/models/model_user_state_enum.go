package models
// UserStateEnum : Which state the user is in
type UserStateEnum string

// List of UserStateEnum
const (
	USERSTATEENUM_ACTIVE UserStateEnum = "ACTIVE"
	USERSTATEENUM_PROVISIONED UserStateEnum = "PROVISIONED"
	USERSTATEENUM_INACTIVE UserStateEnum = "INACTIVE"
)
