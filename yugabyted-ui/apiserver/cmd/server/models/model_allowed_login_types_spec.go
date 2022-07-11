package models

// AllowedLoginTypesSpec - Modify Allowed Login Types payload
type AllowedLoginTypesSpec struct {

	AllowedLoginTypes []LoginTypeInfo `json:"allowed_login_types"`
}
