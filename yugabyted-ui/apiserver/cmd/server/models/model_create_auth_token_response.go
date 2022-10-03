package models

type CreateAuthTokenResponse struct {

	Data AuthTokenData `json:"data"`

	// The jwt issued to the user
	Jwt string `json:"jwt"`
}
