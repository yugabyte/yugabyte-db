package models

type AdminApiTokenResponseData struct {

	// The auth Jwt issued to the admin
	AdminJwt string `json:"adminJwt"`

	Info AdminTokenInfo `json:"info"`
}
