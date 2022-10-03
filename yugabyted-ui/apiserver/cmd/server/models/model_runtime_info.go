package models

type RuntimeInfo struct {

	// The time that the apiserver process was last started (UTC)
	StartTimestamp string `json:"start_timestamp"`

	// Whether or not the server is running
	Alive bool `json:"alive"`
}
