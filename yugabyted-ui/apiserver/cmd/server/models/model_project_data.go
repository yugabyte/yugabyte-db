package models

// ProjectData - Project data
type ProjectData struct {

	// The UUID of the project
	Id string `json:"id"`

	// The name of the project
	Name string `json:"name"`

	Metadata EntityMetadata `json:"metadata"`
}
