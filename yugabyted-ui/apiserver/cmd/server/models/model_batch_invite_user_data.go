package models

// BatchInviteUserData - Batch invite User Data
type BatchInviteUserData struct {

	UserList []BatchInviteUserDataUserList `json:"userList"`
}
