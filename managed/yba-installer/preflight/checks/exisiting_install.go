package checks

import (
	"fmt"
	"os"
)

// InstallExists initalizes the check
var InstallExists = installExistsCheck{"install exists", true}

// TODO: This may not be the correct file, but is good for now.
const installedMarker string = "/opt/yba_installer/.installed"

type installExistsCheck struct {
	name        string
	skipAllowed bool
}

// Name gets the check name.
func (i installExistsCheck) Name() string {
	return i.name
}

// WarningLevel gets the check's warning level.
func (i installExistsCheck) SkipAllowed() bool {
	return i.skipAllowed
}

// Execute runs the check.
func (i installExistsCheck) Execute() Result {
	res := Result{
		Check:  i.name,
		Status: StatusPassed,
	}
	// While err could be some other error (like, permission error, for example), it does not affect
	// the result of InstallExistsCheck. If we can't access the file, it might as well not exist!
	if _, err := os.Stat(installedMarker); err != nil {
		err := fmt.Errorf("no current Yugabyte Anywhere install found")
		res.Error = err
		res.Status = StatusCritical
	}
	return res
}
