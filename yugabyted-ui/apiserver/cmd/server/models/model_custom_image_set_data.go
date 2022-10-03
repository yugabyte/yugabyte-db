package models

// CustomImageSetData - Custom Image Set data
type CustomImageSetData struct {

	Spec CustomImageSetSpec `json:"spec"`

	Info CustomImageSetInfo `json:"info"`

	// List of Custom Images
	CustomImages []CustomImageData `json:"custom_images"`
}
