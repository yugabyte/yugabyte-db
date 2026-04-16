package ybactl

import (
	"os"
	"strings"
	"regexp"
)

var Version = "999.99.9"
var BuildID = "id"

func (y YbaCtlComponent) Version() string {
	return Version
}

func init() {
	var devVersion = regexp.MustCompile(`^.*-b0$`)
	if ((strings.Contains(Version, "PRE_RELEASE") || devVersion.MatchString(Version)) &&
			 os.Getenv("YBA_MODE") == "dev") {
		Version = strings.ReplaceAll(Version, "PRE_RELEASE", BuildID)
		Version = strings.ReplaceAll(Version, "b0", "b" + BuildID)
	}
}
