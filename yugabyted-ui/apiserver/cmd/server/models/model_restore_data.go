package models

// RestoreData - Restore Backup data
type RestoreData struct {

	Spec RestoreSpec `json:"spec"`

	Info RestoreInfo `json:"info"`
}
