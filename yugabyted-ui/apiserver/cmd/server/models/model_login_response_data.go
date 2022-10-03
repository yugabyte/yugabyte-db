package models

type LoginResponseData struct {

	// The auth token issued to the user
	AuthToken string `json:"authToken"`

	// The id associated with the user
	UserId *string `json:"userId"`
}
