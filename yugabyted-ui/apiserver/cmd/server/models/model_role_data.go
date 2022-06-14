package models

// RoleData - RBAC Role Data
type RoleData struct {

	// The UUID of the RBAC role
	Id string `json:"id"`

	// Name of the RBAC role
	Name string `json:"name"`

	// Description of the RBAC role
	Description string `json:"description"`
}
