package models

type BatchInviteUserDataUserList struct {

	InviteUserData InviteUserData `json:"InviteUserData"`

	// user invitiation status
	IsSuccessful bool `json:"is_successful"`

	ErrorMessage *string `json:"error_message"`
}
