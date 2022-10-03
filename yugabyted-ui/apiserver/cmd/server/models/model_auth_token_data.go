package models

// AuthTokenData - Auth Token Data
type AuthTokenData struct {

	Spec AuthTokenSpec `json:"spec"`

	Info AuthTokenInfo `json:"info"`
}
