package models

// AppConfigResponseDataItem - An object containing config path and value
type AppConfigResponseDataItem struct {

	// Configuration path
	Path string `json:"path"`

	// Value corresponding to the path
	Value string `json:"value"`
}
