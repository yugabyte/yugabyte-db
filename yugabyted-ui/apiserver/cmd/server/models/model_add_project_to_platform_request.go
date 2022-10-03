package models

// AddProjectToPlatformRequest - Payload to define project and cloud
type AddProjectToPlatformRequest struct {

	ProjectId string `json:"project_id"`

	Cloud CloudEnum `json:"cloud"`
}
