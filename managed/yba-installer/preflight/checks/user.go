package checks

import (
	"fmt"
	osuser "os/user"

	"github.com/spf13/viper"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/common"
	log "github.com/yugabyte/yugabyte-db/managed/yba-installer/logging"
)

var User = &userCheck{"user", "warning"}

type userCheck struct {
	name, warningLevel string
}

func (u userCheck) Name() string {
	return u.name
}

func (u userCheck) WarningLevel() string {
	return u.warningLevel
}

// Execute will run the user check. During this check, we want to see if the desired username
// either already exists or if we are allowed to create it given sudo privileges
func (u userCheck) Execute() error {
	if !common.HasSudoAccess() {
		log.Debug("Skip user preflight check, we do not use an additional user as non-root")
		return nil
	}
	userName := viper.GetString("serviceUser.username")
	_, err := osuser.Lookup(userName)
	if err == nil {
		log.Info("Found user '" + userName + "', no need to create a user")
		return nil
	}

	// User is not found, fail if we are not allowed to create it. Otherwise, ask for explicit
	// permission. This is to ensure that the customer is okay with the user being created
	// by the install workflow.
	if viper.GetBool("serviceUser.bringOwn") {
		return fmt.Errorf("user '%s' is expected to exist, please create the user or set "+
			"'serviceUser.bringOwn' to false in the yba-installer-input.yml", userName)
	}

	prompt := fmt.Sprintf("Can yba-ctl create the '%s' user on this machine:", userName)
	if !common.UserConfirm(prompt, common.DefaultNone) {
		return fmt.Errorf("please provide user '%s' and try the install again", userName)
	}
	return nil
}
