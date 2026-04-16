package checks

import (
	"fmt"

	"github.com/yugabyte/yugabyte-db/managed/yba-installer/licensing/license"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/licensing/pubkey"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/logging"
)

// License is the check that gets run.
var License = &licenseCheck{"license", false}

type licenseCheck struct {
	name        string
	skipAllowed bool
}

// Name gets the name of the check
func (l licenseCheck) Name() string {
	return l.name
}

// SkipAllowed gets if the check can be skipped.
func (l licenseCheck) SkipAllowed() bool {
	return l.skipAllowed
}

func (l licenseCheck) Execute() Result {
	res := Result{
		Check:  l.name,
		Status: StatusPassed,
	}

	// Dev build can skip validation
	if !pubkey.ValidationRequired {
		logging.Info("License check not required on dev build")
		return res
	}

	lic, err := license.FromInstalledLicense()
	if err != nil {
		res.Error = err
		res.Status = StatusCritical
		return res
	}

	if !lic.Validate() {
		res.Error = fmt.Errorf("invalid license given")
		res.Status = StatusCritical
		return res
	}
	return res
}
