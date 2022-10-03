package models

// LoginTypeInfo - Login type information
type LoginTypeInfo struct {

	LoginType LoginTypeEnum `json:"login_type"`

	IsEnabled bool `json:"is_enabled"`
}
