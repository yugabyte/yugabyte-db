/*
 * Copyright (c) YugabyteDB, Inc.
 */

package checks

import (
	"fmt"
	osuser "os/user"

	"github.com/spf13/viper"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/common"
)

// Python checks to ensure the correct version of python exists
var Python = &pythonCheck{"python", false}

type pythonCheck struct {
	name        string
	skipAllowed bool
}

// Name gets the name of the check
func (p pythonCheck) Name() string {
	return p.name
}

// SkipAllowed gets if the check can be skipped
func (p pythonCheck) SkipAllowed() bool {
	return p.skipAllowed
}

// Execute runs the python check. Ensures we have a valid version of python for yba.
func (p pythonCheck) Execute() Result {
	res := Result{
		Check:  p.name,
		Status: StatusPassed,
	}

	// Root installs run the services as the service user, so validate against that user's python.
	user := ""
	if common.HasSudoAccess() {
		user = viper.GetString("service_username")
		if _, err := osuser.Lookup(user); err != nil {
			// A root install creates the yugabyte user itself, so preflight cannot demand it up
			// front. Warn and defer: createYugabyteUser re-runs ValidatePython right after useradd,
			// and the user check is what fails a custom user the install would never create.
			res.Status = StatusWarning
			res.Error = fmt.Errorf("service user '%s' does not exist yet, deferring the python "+
				"check until the user is created", user)
			return res
		}
	}

	if err := common.ValidatePython(user); err != nil {
		res.Status = StatusCritical
		res.Error = err
	}
	return res
}
