package models

// FaultInjectionSpec - Contains the fault name and entity ref
type FaultInjectionSpec struct {

	FaultName string `json:"fault_name"`

	EntityRef string `json:"entity_ref"`

	UserId *string `json:"user_id"`
}
