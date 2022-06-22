package models

// RuntimeConfigUpdateRequest - Config key-value pairs for runtime config update
type RuntimeConfigUpdateRequest struct {

	Configs []RuntimeConfigSpec `json:"configs"`
}
