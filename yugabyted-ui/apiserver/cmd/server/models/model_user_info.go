package models

// UserInfo - User Information
type UserInfo struct {

	// The UUID of the user
	Id string `json:"id"`

	State UserStateEnum `json:"state"`

	// List of roles associated with the user
	RoleList *[]UserAccountRoleData `json:"role_list"`

	Metadata EntityMetadata `json:"metadata"`

	// List of login types associated with the user
	LoginTypes []LoginTypeEnum `json:"login_types"`
}
