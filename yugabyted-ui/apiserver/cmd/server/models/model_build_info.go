package models

type BuildInfo struct {

	// The hash of the cloud code that is running
	Revision string `json:"revision"`

	// The branch that the cloud code belongs to
	Branch string `json:"branch"`
}
