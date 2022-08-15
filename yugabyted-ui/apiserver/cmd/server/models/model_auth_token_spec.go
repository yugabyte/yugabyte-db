package models

// AuthTokenSpec - Token Specification
type AuthTokenSpec struct {

	// Timestamp when the token will expire (UTC)
	ExpiryTime string `json:"expiry_time"`
}
