package models

// CreateUserRequest - Create user request
type CreateUserRequest struct {

	UserSpec UserSpec `json:"user_spec"`

	// Password associated with the user
	Password string `json:"password"`

	// Token from hCaptcha service
	HcaptchaToken string `json:"hcaptcha_token"`
}
