package ybactl

import (
	"os"
	"strings"
)

var Version = "999.99.9"
var BuildID = "id"

func (y YbaCtlComponent) Version() string {
	return Version
}

func init() {
	if strings.Contains(Version, "PRE_RELEASE") && os.Getenv("YBA_MODE") == "dev" {
		Version = strings.ReplaceAll(Version, "PRE_RELEASE", BuildID)
	}
}
