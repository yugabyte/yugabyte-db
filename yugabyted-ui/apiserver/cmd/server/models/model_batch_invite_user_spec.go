package models

type BatchInviteUserSpec struct {

	UserList []InviteUserSpec `json:"userList"`
}
