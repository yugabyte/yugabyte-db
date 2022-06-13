package models

// CustomImageSpec - Custom Image specification
type CustomImageSpec struct {

	// Name of the custom image in the cloud
	ImageName string `json:"image_name"`

	// Cloud region where the custom image is deployed
	Region string `json:"region"`
}
