package models

// CustomImageData - Custom Image data
type CustomImageData struct {

	Spec CustomImageSpec `json:"spec"`

	Info CustomImageInfo `json:"info"`
}
