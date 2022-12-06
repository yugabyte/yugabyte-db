package checks

import (
	"fmt"
	"os"
)

var InstallExists = installExistsCheck{"install exists", "warning"}

// TODO: This may not be the correct file, but is good for now.
const installedMarker string = "/opt/yba_installer/.installed"

// InstallExistsCheck will check to ensure that an install has already happened.
// This is for the upgrade workflow, as upgrades require an existing install
// to upgrade.
type installExistsCheck struct {
	name         string
	warningLevel string
}

// Name gets the check name.
func (i installExistsCheck) Name() string {
	return i.name
}

// WarningLevel gets the check's warning level.
func (i installExistsCheck) WarningLevel() string {
	return i.warningLevel
}

// Execute runs the check.
func (i installExistsCheck) Execute() error {
	// While err could be some other error (like, permission error, for example), it does not affect
	// the result of InstallExistsCheck. If we can't access the file, it might as well not exist!
	if _, err := os.Stat(installedMarker); err != nil {
		return fmt.Errorf("no current Yugabyte Anywhere install found")
	}
	return nil
}
