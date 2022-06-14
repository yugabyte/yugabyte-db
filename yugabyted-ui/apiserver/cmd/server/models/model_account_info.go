package models

// AccountInfo - Account Info
type AccountInfo struct {

	// The UUID of the account
	Id string `json:"id"`

	// UUID of the owner user of the account
	OwnerId string `json:"owner_id"`

	Roles []RoleData `json:"roles"`

	Metadata EntityMetadata `json:"metadata"`
}
