package models

type LoginRequest struct {

	// The email associated with the user
	Email string `json:"email"`

	// The password associated with the user
	Password string `json:"password"`
}
