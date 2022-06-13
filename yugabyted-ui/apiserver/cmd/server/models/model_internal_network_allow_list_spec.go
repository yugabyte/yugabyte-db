package models

// InternalNetworkAllowListSpec - Allow list for client connections to database
type InternalNetworkAllowListSpec struct {

	// Name of the allow list group
	Name string `json:"name"`

	// A short description of the allow list group
	Description string `json:"description"`

	// Whether this allow list entry will be visible to users or not
	IsInternal bool `json:"is_internal"`

	// The list of CIDRs
	AllowList []string `json:"allow_list"`
}
