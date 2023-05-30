package checks

import (
	"fmt"
	osuser "os/user"

	"github.com/spf13/viper"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/common"
	log "github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/logging"
)

// User is the username change
var User = &userCheck{"user", false}

type userCheck struct {
	name        string
	skipAllowed bool
}

// Name gets the name of the check
func (u userCheck) Name() string {
	return u.name
}

// SkipAllowed returns if the check is skippable
func (u userCheck) SkipAllowed() bool {
	return u.skipAllowed
}

// Execute will run the user check. During this check, we want to see if the desired username
// either already exists or if we are allowed to create it given sudo privileges
func (u userCheck) Execute() Result {
	res := Result{
		Check:  u.name,
		Status: StatusPassed,
	}
	if !common.HasSudoAccess() {
		log.Debug("Skip user preflight check, we do not use an additional user as non-root")
		return res
	}
	uname := viper.GetString("service_username")
	log.Debug("Checking user " + uname + " exists")
	_, err := osuser.Lookup(uname)
	if err == nil {
		log.Info("Found user '" + uname + "', no need to create a user")
		return res
	}

	// User is not found, fail if we are not allowed to create it. Otherwise, ask for explicit
	// permission. This is to ensure that the customer is okay with the user being created
	// by the install workflow.
	if uname != "yugabyte" {
		res.Error = fmt.Errorf("custom user '%s' is expected to exist, please create the user or "+
			"update the config back to the default user.", uname)
		res.Status = StatusCritical
	}
	prompt := fmt.Sprintf("Create the '%s' user to run YugabyteDB Anywhere services:", uname)
	if !common.UserConfirm(prompt, common.DefaultNone) {
		log.Fatal("please provide user '" + uname + "' and try the install again")
	}
	return res
}
