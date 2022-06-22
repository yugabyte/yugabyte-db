package models

// InviteUserData - Invite User Data
type InviteUserData struct {

	Spec InviteUserSpec `json:"spec"`

	Info UserInfo `json:"info"`
}
