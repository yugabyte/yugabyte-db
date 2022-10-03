package models

// AdminTokenInfo - Admin Token Info
type AdminTokenInfo struct {

	// The UUID of the token
	Id string `json:"id"`

	// UUID of the user that the admin is impersonating
	ImpersonatedUserId *string `json:"impersonated_user_id"`

	// Timestamp when the token will expire (UTC)
	ExpiryTime string `json:"expiry_time"`

	// The email of the admin user who issued the token
	ImpersonatingUserEmail *string `json:"impersonating_user_email"`

	// The service account email used to issue the jwt
	IssuingAuthority *string `json:"issuing_authority"`

	Metadata EntityMetadata `json:"metadata"`
}
