package models

// FaultInjection - Contains the entity ref and user id
type FaultInjection struct {

	EntityRef string `json:"entity_ref"`

	UserId *string `json:"user_id"`
}
