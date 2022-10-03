package models

// InternalNodeData - Internal node data
type InternalNodeData struct {

	// Name of the node (as recognized by platform)
	Name string `json:"name"`

	// Private IP of the node
	PrivateIp *string `json:"private_ip"`

	// Region the node belongs to
	Region string `json:"region"`
}
