package models

// DefaultInternalTagsUpdateData - Default internal tags update data
type DefaultInternalTagsUpdateData struct {

	// Email prefix of the user
	EmailPrefix string `json:"email_prefix"`

	// Value of yb_dept tag
	YbDept string `json:"yb_dept"`

	// Value of yb_task tag
	YbTask string `json:"yb_task"`

	// Whether the update was successful or not
	Success bool `json:"success"`

	// Error message when update fails
	Error string `json:"error"`
}
