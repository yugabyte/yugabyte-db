package models

// BatchAddAccountSoftwareTrackRequest - Batch Account Specific Software Release Track Data
type BatchAddAccountSoftwareTrackRequest struct {

	TrackList []string `json:"track_list"`
}
