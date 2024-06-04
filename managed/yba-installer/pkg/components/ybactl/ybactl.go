package ybactl

import (
	"errors"
	"fmt"
	"io/fs"
	"os"
	"path/filepath"

	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/common"
	log "github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/logging"
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
	common.Version = yc.Version()
	err := common.MkdirAll(common.YbactlInstallDir(), common.DirMode)
	return err
}

// Install yba-ctl to the install dir (/opt/yba-ctl for root installs).
// This installs yba-ctl and the version file into /opt/yba-ctl, in addition to symlinking to
// /usr/bin/yba-ctl.
func (yc *YbaCtlComponent) Install() error {
	log.Info("Installing yba-ctl")

	for _, file := range []string{common.GoBinaryName, common.VersionMetadataJSON} {
		// Remove the existing file and ignore not exists errors.
		err := os.Remove(filepath.Join(common.YbactlInstallDir(), file))
		if err != nil && !errors.Is(err, os.ErrNotExist) {
			return fmt.Errorf("failed to remove existing file %s: %w",
				filepath.Join(common.YbactlInstallDir(), file), err)
		}
		fp := common.AbsoluteBundlePath(file)
		if err := common.Copy(fp, common.YbactlInstallDir(), false, true); err != nil {
			return fmt.Errorf("failed to copy %s during yba-ctl install: %w", fp, err)
		}
	}

	// Symlink yba-ctl into a bin directory that should be in the path
	if common.HasSudoAccess() {
		err := common.CreateSymlink(common.YbactlInstallDir(), "/usr/bin", common.GoBinaryName)
		if err != nil {
			return fmt.Errorf("could not create symlink: %w", err)
		}
	} else {

		// Create the home bin directory
		usrBin, err := createHomeBinDir()
		if err != nil {
			return fmt.Errorf("install failed to setup home bin directory: %w", err)
		}

		// Create the symlink
		err = common.CreateSymlink(common.YbactlInstallDir(), usrBin, common.GoBinaryName)
		if err != nil {
			return fmt.Errorf("could not create symlink: %w", err)
		}
		log.Info("Installed yba-ctl at " + usrBin + ". Please ensure this is in your PATH env, or " +
			"move yba-ctl into a directory that is already in your path.")
	}

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

func createHomeBinDir() (string, error) {
	home, err := os.UserHomeDir()
	if err != nil {
		return "", fmt.Errorf("could not determine home directory: %w", err)
	}
	usrBin := filepath.Join(home, ".local", "bin")
	if err := common.MkdirAll(usrBin, common.DirMode); err != nil {
		return usrBin, fmt.Errorf("could not create directory '%s': %w", usrBin, err)
	}
	return usrBin, nil
}
