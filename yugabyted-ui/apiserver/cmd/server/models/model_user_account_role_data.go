package models

type UserAccountRoleData struct {

	AccountId string `json:"account_id"`

	// Role Ids
	RoleIds []string `json:"role_ids"`
}
