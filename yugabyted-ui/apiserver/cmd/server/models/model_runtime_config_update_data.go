package models

// RuntimeConfigUpdateData - Runtime Config data after update
type RuntimeConfigUpdateData struct {

	ConfigKey string `json:"config_key"`

	ConfigValue *string `json:"config_value"`

	Success bool `json:"success"`

	// Success/Failure message
	Error *string `json:"error"`
}
