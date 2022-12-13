package common

import (
	"errors"
	"os"
	"path/filepath"

	"github.com/spf13/viper"
	log "github.com/yugabyte/yugabyte-db/managed/yba-installer/logging"
)

/* Directory Structure for Yugabyte Installs:
*
* A default install will be laid out roughly as follows
* /opt/yugabyte/
*               data/
*                    logs and other long-lived data (like postgres)
*               one/
*                   Install files
*               two/ # Nothing as it is our alt-install directory
*               active # Symlink pointing to one
*
* Base Install:   /opt/yugabyte
* Install root:   /opt/yugabyte/one # Note: After upgrade this can be /opt/yugabyte/two
* Data directory: /opt/yugabyte/data
* Active Symlink: /opt/yugabyte/active
*
* GetInstallRoot will return the CORRECT install root for our workflow (one or two)
# GetBaseInstall will return the base install. NOTE: the config has this as "installRoot"
*/

// ALl of our install files and directories.
const (
	installMarkerName  string = ".install_marker"
	installLocationOne string = "one"
	installLocationTwo string = "two"
	InstallSymlink     string = "active"
)

// Directory names for config and cron files.
const (
	ConfigDir = "templates" // directory name service config templates are (relative to yba-ctl)
	CronDir   = "cron"      // directory name non-root cron scripts are (relative to yba-ctl)
)

// SystemdDir service file directory.
const SystemdDir string = "/etc/systemd/system"

// GetBaseInstall returns the base install directory, as defined by the user
func GetBaseInstall() string {
	return viper.GetString("installRoot")
}

// GetInstallRoot returns the InstallRoot where YBA is installed.
func GetInstallRoot() string {
	return dm.WorkingDirectory()
}

// GetActiveSymlink will return the symlink file name
func GetActiveSymlink() string {
	return dm.ActiveSymlink()
}

// GetInstallVersionDir returns the yba_installer directory inside InstallRoot
func GetInstallVersionDir() string {
	return dm.WorkingDirectory() + "/yba_installer-" + GetVersion()
}

// GetInstallOne is the full path to install location one
func GetInstallOne() string {
	return filepath.Join(viper.GetString("installRoot"), installLocationOne)
}

// GetInstallTwo is the full path to install location two
func GetInstallTwo() string {
	return filepath.Join(viper.GetString("installRoot"), installLocationTwo)
}

// CreateInstallMarker Creates the marker file in the active directory
func CreateInstallMarker() error {
	return dm.CreateInstallMarker()
}

// Default the directory manager to using the install workflow.
var dm directoryManager = directoryManager{
	Workflow: workflowInstall,
}

// SetWorkflowUpgrade changes the workflow from install to upgrade.
func SetWorkflowUpgrade() {
	dm.Workflow = workflowUpgrade
}

type workflow string

const (
	workflowInstall workflow = "install"
	workflowUpgrade workflow = "upgrade"
)

type directoryManager struct {
	Workflow workflow
}

func (dm directoryManager) BaseInstall() string {
	return viper.GetString("installRoot")
}

// WorkingDirectory returns the directory the workflow should be using
// the active directory for install case, and the inactive for upgrade case.
func (dm directoryManager) WorkingDirectory() string {
	switch dm.Workflow {
	case workflowInstall:
		return filepath.Join(dm.BaseInstall(), dm.getInstallName(true))
	case workflowUpgrade:
		return filepath.Join(dm.BaseInstall(), dm.getInstallName(false))
	default:
		panic("unsupported workflow " + dm.Workflow)
	}
}

// GetActiveSymlink will return the symlink file name
func (dm directoryManager) ActiveSymlink() string {
	return filepath.Join(dm.BaseInstall(), InstallSymlink)
}

// CreateInstallMarker Creates the marker file in the active directory
func (dm directoryManager) CreateInstallMarker() error {
	activeLoc := filepath.Base(dm.WorkingDirectory())
	return os.WriteFile(filepath.Join(dm.BaseInstall(), installMarkerName),
		[]byte(activeLoc), os.ModePerm)
}

// CleanAltInstall will remove the non-active install directory, in prep for upgrade
func (dm directoryManager) CleanAltInstall() error {
	// First remove the alt install directory if it exists
	if _, err := os.Stat(dm.WorkingDirectory()); !errors.Is(err, os.ErrNotExist) {
		if err := os.RemoveAll(dm.WorkingDirectory()); err != nil {
			log.Info("failed to clean " + dm.WorkingDirectory())
			return err
		}
	}
	return os.MkdirAll(dm.WorkingDirectory(), os.ModePerm)
}
func (dm directoryManager) getInstallName(active bool) string {
	data, err := os.ReadFile(filepath.Join(dm.BaseInstall(), installMarkerName))
	if err != nil {
		if errors.Is(err, os.ErrNotExist) {
			return installLocationOne
		}
		log.Fatal("failed to read " + filepath.Join(dm.BaseInstall(), installMarkerName) +
			": " + err.Error())
	}
	switch string(data) {
	case installLocationOne:
		if active {
			return installLocationOne
		}
		return installLocationTwo
	case installLocationTwo:
		if active {
			return installLocationTwo
		}
		return installLocationOne
	default:
		log.Fatal("Invalid install location " + string(data))
		// log.Fatal is the same as panic, but the compiler doesn't catch that so we need a return.
		return ""
	}
}
