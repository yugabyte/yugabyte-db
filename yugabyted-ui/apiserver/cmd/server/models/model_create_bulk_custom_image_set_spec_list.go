package models

// CreateBulkCustomImageSetSpecList - List of custom image sets for bulk creation
type CreateBulkCustomImageSetSpecList struct {

	CustomImageSetList []CreateBulkCustomImageSetSpec `json:"custom_image_set_list"`
}
