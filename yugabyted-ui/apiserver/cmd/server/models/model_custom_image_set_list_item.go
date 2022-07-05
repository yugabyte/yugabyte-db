package models

// CustomImageSetListItem - Custom Image Set List Item
type CustomImageSetListItem struct {

	Id string `json:"id"`

	CloudType CloudEnum `json:"cloud_type"`

	// Version of the DB present in the images of this custom image set
	DbVersion string `json:"db_version"`

	// Base image name on which the images of this custom image set are based on
	BaseImageName string `json:"base_image_name"`

	// Build reference that generated images of this custom image set
	BuildReference string `json:"build_reference"`

	Architecture ArchitectureEnum `json:"architecture"`

	// Denotes whether the images of this set are to be used for <cloud, db_version> combination or not
	IsDefault bool `json:"is_default"`

	Metadata EntityMetadata `json:"metadata"`
}
