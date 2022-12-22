package checks

import (
	"fmt"
	"os"
	"path/filepath"

	"github.com/yugabyte/yugabyte-db/managed/yba-installer/common"
	log "github.com/yugabyte/yugabyte-db/managed/yba-installer/logging"
)

// InputFile initializes the check
var InputFile = inputFileCheck{"input", false}

type inputFileCheck struct {
	name        string
	skipAllowed bool
}

// Name gets the check name
func (i inputFileCheck) Name() string {
	return i.name
}

func (i inputFileCheck) SkipAllowed() bool {
	return i.skipAllowed
}

func (i inputFileCheck) Execute() Result {
	res := Result{
		Check:  i.name,
		Status: StatusPassed,
	}

	if !common.UserConfirm(
		fmt.Sprintf("Is %s present and in the desired state?", common.InputFile), common.DefaultYes) {
		res.Error = fmt.Errorf("user abort")
		res.Status = StatusCritical
	}

	if _, err := os.Stat(common.InputFile); err != nil {
		err := fmt.Errorf("%s is not present. Copying over reference", common.InputFile)
		res.Error = err
		res.Status = StatusWarning

		// Copy over reference yaml and reinit viper.
		// TODO: performing actions in preflight checks doesn't seem very clean
		common.CreateDir(filepath.Dir(common.InputFile), 0744)
		common.CopyFileGolang(common.GetReferenceYaml(), common.InputFile)
		os.Chmod(common.InputFile, 0600)
		common.InitViper()

		// TODO(minor): we might want to log restarts and other cmds but
		// this preflight check is only called for install/upgrade
		log.AddOutputFile(common.YbaCtlLogFile)

	}

	return res
}
