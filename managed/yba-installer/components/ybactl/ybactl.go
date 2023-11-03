package ybactl

import (
	"errors"
	"fmt"
	"io/fs"
	"os"
	"path/filepath"

	"github.com/yugabyte/yugabyte-db/managed/yba-installer/common"
	log "github.com/yugabyte/yugabyte-db/managed/yba-installer/logging"
)

// YbaCtlComponent implements the installer for yba-ctl.
type YbaCtlComponent struct {
}

// New initializes the yba ctl installer.
func New() *YbaCtlComponent {
	return &YbaCtlComponent{}
}

// Setup will create the base install directory needed for initialization.
func (yc *YbaCtlComponent) Setup() error {
	err := common.MkdirAll(common.YbactlInstallDir(), common.DirMode)
	return err
}

// Install yba-ctl to the install dir (/opt/yba-ctl for root installs).
func (yc *YbaCtlComponent) Install() error {
	log.Info("Installing yba-ctl")
	// Symlink at /usr/bin/yba-ctl -> /opt/yba-ctl/yba-ctl -> actual yba-ctl

	common.CreateSymlink(common.GetInstallerSoftwareDir(), common.YbactlInstallDir(),
		common.GoBinaryName)

	if common.HasSudoAccess() {
		common.CreateSymlink(common.YbactlInstallDir(), "/usr/bin", common.GoBinaryName)
	} else {
		home, err := os.UserHomeDir()
		if err != nil {
			log.Fatal("unable to get user home directory: " + err.Error())
		}
		usrBin := filepath.Join(home, "bin")
		common.MkdirAll(usrBin, 0755)
		common.CreateSymlink(common.YbactlInstallDir(), usrBin, common.GoBinaryName)
		log.Info("Installed yba-ctl at " + usrBin + ". Please ensure this is in your PATH env, or " +
			"move yba-ctl into a directory that is already in your path.")
	}

	yc.MarkYBAInstallDone()
	return nil
}

// Uninstall cleans all yba-ctl files.
func (yc *YbaCtlComponent) Uninstall() error {
	return nil
}

// Status gets the basic status of yba-ctl.
func (yc *YbaCtlComponent) Status() common.Status {
	var status common.StatusType = common.StatusRunning

	// Check if we are installed.
	if _, err := os.Stat(common.YbactlInstallDir()); errors.Is(err, fs.ErrNotExist) {
		status = common.StatusNotInstalled
	}
	return common.Status{
		Service:    "yba-ctl",
		LogFileLoc: common.YbactlLogFile(),
		Status:     status,
	}
}

// MarkYbaInstallStart sets the .installing marker.
func (yc *YbaCtlComponent) MarkYBAInstallStart() error {
	// .installStarted written at the beginning of Installations, and renamed to
	// .installCompleted at the end of the install. That way, if an install fails midway,
	// the operations can be tried again in an idempotent manner. We also write the mode
	// of install to this file so that we can disallow installs between types (root ->
	// non-root and non-root -> root both prohibited).
	var data []byte
	if common.HasSudoAccess() {
		data = []byte("root\n")
	} else {
		data = []byte("non-root\n")
	}
	return os.WriteFile(common.YbaInstallingMarker(), data, 0666)
}

// MarkYbaInstallDone sets the .installed marker. This also deletes the .installing marker.
func (yc *YbaCtlComponent) MarkYBAInstallDone() error {
	_, err := os.Stat(common.YbaInstalledMarker())
	if err == nil {
		return nil
	}
	if !os.IsNotExist(err) {
		log.Fatal(fmt.Sprintf("Error querying file status %s : %s", common.YbaInstalledMarker(), err))
	}
	common.RenameOrFail(common.YbaInstallingMarker(), common.YbaInstalledMarker())
	return nil
}
