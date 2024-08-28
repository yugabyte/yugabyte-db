package checks

import (
	"fmt"

	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/common"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/common/shell"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/logging"
)

var SystemdUserSupported = &systemdUserSupportedCheck{"systemd_user_supported", false}

type systemdUserSupportedCheck struct {
	name        string
	skipAllowed bool
}

func (s systemdUserSupportedCheck) Name() string      { return s.name }
func (s systemdUserSupportedCheck) SkipAllowed() bool { return s.skipAllowed }

func (s systemdUserSupportedCheck) Execute() Result {
	res := Result{
		Check:  s.name,
		Status: StatusPassed,
	}

	// Check if the user has systemd user support. If the command fails, we can assume --user is not
	// supported on this os.
	if !common.HasSudoAccess() {
		if out := shell.Run("systemctl", "--user", "status"); !out.SucceededOrLog() {
			res.Status = StatusCritical
			res.Error = fmt.Errorf("systemctl --user is not supported: %w", out.Error)
		}
	} else {
		logging.Debug("Skipping systemd user check as user has sudo access")
		res.Status = StatusSkipped
	}
	return res
}
