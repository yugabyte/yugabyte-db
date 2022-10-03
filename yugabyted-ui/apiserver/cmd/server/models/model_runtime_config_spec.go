package models

// RuntimeConfigSpec - Runtime Config value specification for a path
type RuntimeConfigSpec struct {

	ConfigKey string `json:"config_key"`

	ConfigValue string `json:"config_value"`

	OverrideEntities bool `json:"override_entities"`
}
