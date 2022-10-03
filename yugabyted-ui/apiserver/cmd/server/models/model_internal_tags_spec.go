package models

// InternalTagsSpec - Internal tags spec
type InternalTagsSpec struct {

	// Email prefix of the user
	EmailPrefix string `json:"email_prefix"`

	// Value of yb_dept tag
	YbDept *string `json:"yb_dept"`

	// Value of yb_task tag
	YbTask *string `json:"yb_task"`

	// Value of yb_owner tag
	YbOwner *string `json:"yb_owner"`

	// Value of yb_customer tag
	YbCustomer *string `json:"yb_customer"`

	// If true, then the default tags will be overridden
	OverrideDefault bool `json:"override_default"`
}
