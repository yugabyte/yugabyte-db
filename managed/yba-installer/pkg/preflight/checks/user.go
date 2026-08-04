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
	uname := viper.GetString("service_username")

	// In the non-root case, just validate the the configured user is the same as the current user
	if !common.HasSudoAccess() {
		curr, err := osuser.Current()
		if err != nil {
			res.Status = StatusCritical
			res.Error = err
			return res
		}
		if uname != curr.Username {
			res.Status = StatusCritical
			res.Error = fmt.Errorf("nonroot install run as user '%s' cannot install for user '%s'. "+
				"Please update the config or log in as the currect user", curr.Username, uname)
			return res
		}
		return res
	}
	// Check for root case
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
