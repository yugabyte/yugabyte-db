package models

// AddPlatformRequest - Payload to define a platform
type AddPlatformRequest struct {

	PlatformData PlatformData `json:"platform_data"`

	Credentials string `json:"credentials"`

	AdminApiKey string `json:"admin_api_key"`
}
