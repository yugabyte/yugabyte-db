package checks

import (
	"path/filepath"
	"fmt"
	"os"

	"github.com/yugabyte/yugabyte-db/managed/yba-installer/common"
)

// InputFile initializes the check
var InputFile = inputFileCheck{"input", false}

type inputFileCheck struct {
	name				string
	skipAllowed	bool
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
		Check: i.name,
		Status: StatusPassed,
	}

	common.UserConfirm(
		fmt.Sprintf("Is %s present and in the desired state?", common.InputFile), common.DefaultYes)

	if _, err := os.Stat(common.InputFile); err != nil {
		err := fmt.Errorf("%s is not present. Copying over reference.", common.InputFile)
		res.Error = err
		res.Status = StatusWarning

		// Copy over reference yaml and reinit viper.
		common.CreateDir(filepath.Dir(common.InputFile), 0600)
		common.CopyFileGolang(common.GetReferenceYaml(), common.InputFile)
		common.InitViper()
	}

	return res
}
