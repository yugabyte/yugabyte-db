package models

// ChangePasswordRequest - Change password request payload
type ChangePasswordRequest struct {

	// Current password associated with the user
	CurrentPassword string `json:"current_password"`

	// New password to associate with the user
	NewPassword string `json:"new_password"`
}
