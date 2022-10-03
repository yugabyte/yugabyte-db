package models

// CreateBulkCustomImageSetStatus - Custom Image bulk response Status
type CreateBulkCustomImageSetStatus struct {

	Spec CustomImageSetSpec `json:"spec"`

	Info CreateBulkCustomImageSetResponseInfo `json:"info"`
}
