package models

// CustomImageInfo - Custom Image Information
type CustomImageInfo struct {

	// The UUID of the custom image
	Id string `json:"id"`

	Metadata EntityMetadata `json:"metadata"`
}
