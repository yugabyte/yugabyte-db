package models
// SsoEventType : SSO Event Type
type SsoEventType string

// List of SsoEventType
const (
	SSOEVENTTYPE_SIGNUP SsoEventType = "SIGNUP"
	SSOEVENTTYPE_LOGIN SsoEventType = "LOGIN"
	SSOEVENTTYPE_INVITE SsoEventType = "INVITE"
)
