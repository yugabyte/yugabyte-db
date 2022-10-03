package models

type AppConfigResponse struct {

	// A list of configuration paths and corresponding values
	Data []AppConfigResponseDataItem `json:"data"`
}
