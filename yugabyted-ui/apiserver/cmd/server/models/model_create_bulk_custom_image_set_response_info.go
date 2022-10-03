package models

// CreateBulkCustomImageSetResponseInfo - Custom Image Set Response Information
type CreateBulkCustomImageSetResponseInfo struct {

	// The UUID of the custom image set
	Id string `json:"id"`

	// Whether the creation of all custom images in the set is successful or not
	Success bool `json:"success"`

	// Error message in case of failure
	Error *string `json:"error"`

	Metadata EntityMetadata `json:"metadata"`
}
