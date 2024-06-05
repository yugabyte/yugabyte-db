package checks

import (
	"errors"
	"fmt"
	"os"

	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/common"
	log "github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/logging"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/ybactlstate"
)

// InstallExists initalizes the check
var InstallExists = installExistsCheck{"install-exists", true}

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
	log.Debug("Checking for install marker file " + common.YbaInstalledMarker())

	state, err := ybactlstate.Initialize()
	// Can have no state if upgrading from a version before state existed. Instead check for marker
	// file.
	if err != nil {
		if _, err := os.Stat(common.YbaInstalledMarker()); err != nil {
			err := fmt.Errorf("no current YugabyteDB Anywhere install found")
			res.Error = err
			res.Status = StatusCritical
		}
	} else if !state.CurrentStatus.TransitionValid(ybactlstate.UpgradingStatus) {
		err = errors.New("cannot initiate upgrade when current status is " +
			state.CurrentStatus.String())
		res.Error = err
		res.Status = StatusCritical
	}
	return res
}

// InstallNotExists initializes check to make sure this is a fresh install.
var InstallNotExists = installNotExistsCheck{"install does not exist", false}

type installNotExistsCheck struct {
	name        string
	skipAllowed bool
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
		Check:  i.name,
		Status: StatusPassed,
	}

	// Error if the file exists because that indicates an install has already been completed.
	log.Debug("Checking for install marker file " + common.YbaInstalledMarker())
	if _, err := os.Stat(common.YbaInstalledMarker()); err == nil {
		err := fmt.Errorf("found existing YugabyteDB Anywhere install")
		res.Error = err
		res.Status = StatusCritical
	}
	return res
}
