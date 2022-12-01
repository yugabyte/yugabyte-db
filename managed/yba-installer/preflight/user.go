package preflight

import (
	"fmt"
	osuser "os/user"

	"github.com/spf13/viper"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/common"
	log "github.com/yugabyte/yugabyte-db/managed/yba-installer/logging"
)

var user Preflight = User{"user", "warning"}

type User struct {
	name, warningLevel string
}

func (u User) Name() string {
	return u.name
}

func (u User) WarningLevel() string {
	return u.warningLevel
}

// Execute will run the user check. During this check, we want to see if the desired username
// either already exists or if we are allowed to create it given sudo privileges
func (u User) Execute() {
	if !common.HasSudoAccess() {
		log.Debug("Skip user preflight check, we do not use an additional user as non-root")
		return
	}
	uname := viper.GetString("service_username")
	_, err := osuser.Lookup(uname)
	if err == nil {
		log.Info("Found user '" + uname + "', no need to create a user")
		return
	}

	// User is not found, fail if we are not allowed to create it. Otherwise, ask for explicit
	// permission. This is to ensure that the customer is okay with the user being created
	// by the install workflow.
	if uname != "yugabyte" {
		log.Fatal("user '" + uname + "' is expected to exist, please create the user or set " +
			"'serviceUser.bringOwn' to false in the yba-installer-input.yml")
	}
	prompt := fmt.Sprintf("Can yba-ctl create the '%s' user on this machine:", uname)
	if !common.UserConfirm(prompt, common.DefaultNone) {
		log.Fatal("please provide user '" + uname + "' and try the install again")
	}
}

func init() {
	RegisterPreflightCheck(user)
}
