package models

type SsoRedirectUrlResponse struct {

	SsoType SsoType `json:"sso_type"`

	SsoEventType SsoEventType `json:"sso_event_type"`

	SsoRedirectUrl string `json:"sso_redirect_url"`
}
