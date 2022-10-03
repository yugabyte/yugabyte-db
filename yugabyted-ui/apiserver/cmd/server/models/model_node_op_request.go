package models

// NodeOpRequest - Node Operation Request
type NodeOpRequest struct {

	// Name of the node to be operated on
	NodeName string `json:"node_name"`

	Action NodeOpEnum `json:"action"`
}
