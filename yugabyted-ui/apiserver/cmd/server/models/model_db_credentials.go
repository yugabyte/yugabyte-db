package models

// DbCredentials - username/password tuple
type DbCredentials struct {

	// username
	Username string `json:"username"`

	// password
	Password string `json:"password"`
}
