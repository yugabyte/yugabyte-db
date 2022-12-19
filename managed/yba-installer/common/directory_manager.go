package common

import (
	"errors"
	"os"
	"path/filepath"

	"github.com/spf13/viper"
	log "github.com/yugabyte/yugabyte-db/managed/yba-installer/logging"
)

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
	CronDir   = "cron"      // directory where non-root cron scripts are (relative to yba-ctl)
)

// SystemdDir service file directory.
const SystemdDir string = "/etc/systemd/system"

// GetBaseInstall returns the base install directory, as defined by the user
func GetBaseInstall() string {
	return viper.GetString("installRoot")
}

var installRoot string // cache for get install root

// GetInstallRoot returns the InstallRoot where YBA is installed.
func GetInstallRoot() string {
	if installRoot == "" {
		baseRoot := GetBaseInstall()
		activeInstall := getActiveInstallName(baseRoot)
		installRoot = filepath.Join(baseRoot, activeInstall)
	}
	return installRoot
}

// GetActiveSymlink will return the symlink file name
func GetActiveSymlink() string {
	return filepath.Join(GetBaseInstall(), InstallSymlink)
}

// GetInstallVersionDir returns the yba_installer directory inside InstallRoot
func GetInstallVersionDir() string {
	return GetInstallRoot() + "/yba_installer-" + GetVersion()
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
	activeLoc := getActiveInstallName(GetBaseInstall())
	return os.WriteFile(filepath.Join(GetBaseInstall(), installMarkerName),
		[]byte(activeLoc), os.ModePerm)
}

func getActiveInstallName(base string) string {
	data, err := os.ReadFile(filepath.Join(base, installMarkerName))
	if err != nil {
		if errors.Is(err, os.ErrNotExist) {
			return installLocationOne
		}
		log.Fatal("failed to read " + filepath.Join(base, installMarkerName) + ": " + err.Error())
	}
	switch string(data) {
	case installLocationOne:
		return installLocationOne
	case installLocationTwo:
		return installLocationTwo
	default:
		log.Fatal("Invalid install location " + string(data))
		// log.Fatal is the same as panic, but the compiler doesn't catch that so we need a return.
		return ""
	}
}
