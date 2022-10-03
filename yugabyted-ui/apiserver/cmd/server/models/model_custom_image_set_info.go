package models

// CustomImageSetInfo - Custom Image Set Information
type CustomImageSetInfo struct {

	// The UUID of the custom image set
	Id string `json:"id"`

	Metadata EntityMetadata `json:"metadata"`
}
