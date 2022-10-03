package models

// ModifyUserRoleRequest - Modify user role request payload
type ModifyUserRoleRequest struct {

	// New role to associate with the user
	RoleId string `json:"role_id"`
}
