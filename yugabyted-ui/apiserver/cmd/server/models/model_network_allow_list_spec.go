package models

// NetworkAllowListSpec - Allow list for client connections to database
type NetworkAllowListSpec struct {

	// Name of the allow list group
	Name string `json:"name"`

	// A short description of the allow list group
	Description string `json:"description"`

	// The list of CIDRs
	AllowList []string `json:"allow_list"`
}
