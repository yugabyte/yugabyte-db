package checks

import (
	"fmt"
	"os"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/common"
)

// InstallExists initalizes the check
var InstallExists = installExistsCheck{"install exists", true}

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
	if _, err := os.Stat(common.InstalledFile); err != nil {
		err := fmt.Errorf("no current Yugabyte Anywhere install found")
		res.Error = err
		res.Status = StatusCritical
	}
	return res
}

// InstallNotExists initializes check to make sure this is a fresh install.
var InstallNotExists = installNotExistsCheck{"install does not exist", false}

type installNotExistsCheck struct {
	name				string
	skipAllowed	bool
}

// Name gets the check name.
func (i installNotExistsCheck) Name() string {
	return i.name
}

// SkipAllowed determines if check can be skipped.
func (i installNotExistsCheck) SkipAllowed() bool {
	return i.skipAllowed
}

// Execute runs the check.
func (i installNotExistsCheck) Execute() Result {
	res := Result{
		Check: i.name,
		Status: StatusPassed,
	}

	// Error if the file exists because that indicates an install has already been completed.
	if _, err := os.Stat(common.InstalledFile); err == nil {
		err := fmt.Errorf("found existing Yugabyte Anywhere install")
		res.Error = err
		res.Status = StatusCritical
	}
	return res
}
