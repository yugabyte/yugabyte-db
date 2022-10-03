package models
// LoginTypeEnum : Login type associated with a user
type LoginTypeEnum string

// List of LoginTypeEnum
const (
	LOGINTYPEENUM_LOCAL LoginTypeEnum = "LOCAL"
	LOGINTYPEENUM_GOOGLE LoginTypeEnum = "GOOGLE"
	LOGINTYPEENUM_GITHUB LoginTypeEnum = "GITHUB"
	LOGINTYPEENUM_LINKEDIN LoginTypeEnum = "LINKEDIN"
)
