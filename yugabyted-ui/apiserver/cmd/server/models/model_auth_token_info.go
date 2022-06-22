package models

// AuthTokenInfo - Auth Token Info
type AuthTokenInfo struct {

	// The UUID of the token
	Id string `json:"id"`

	// Email of the user who issued the jwt
	Issuer string `json:"issuer"`

	Metadata EntityMetadata `json:"metadata"`
}
