package models

// UserTutorialSpec - User Tutorial Spec
type UserTutorialSpec struct {

	IsEnabled bool `json:"is_enabled"`

	Entities []UserTutorialSpecEntity `json:"entities"`

	States []UserTutorialState `json:"states"`
}
