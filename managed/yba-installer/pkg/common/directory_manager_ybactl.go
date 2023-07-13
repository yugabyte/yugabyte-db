package common

import (
	"os"
	"path/filepath"
)

// YbactlRootInstallDir is the install directory of yba-ctl for root installs
const YbactlRootInstallDir string = "/opt/yba-ctl"

var ycp ybaCtlPaths = newYbaCtlPaths()

func YbactlInstallDir() string {
	return ycp.InstallDir()
}

func InputFile() string {
	return ycp.InputFile()
}

func YbactlLogFile() string {
	return ycp.LogFile()
}

func LicenseFile() string {
	return ycp.LicenseFile()
}

func YbaInstallingMarker() string {
	return ycp.YbaInstallingMarker()
}

func YbaInstalledMarker() string {
	return ycp.YbaInstalledMarker()
}

// PgpassPath is the location of the .pgpass file
func PgpassPath() string {
	return filepath.Join(ycp.InstallDir(), ".pgpass")
}

type ybaCtlPaths struct {
	installBase string
}

func newYbaCtlPaths() ybaCtlPaths {
	rootDir := "/opt"
	if !HasSudoAccess() {
		var err error
		rootDir, err = os.UserHomeDir()
		if err != nil {
			panic(err)
		}
	}
	return ybaCtlPaths{rootDir}
}

// InstallDir gets the yba-ctl install directory.
func (ycp ybaCtlPaths) InstallDir() string {
	return filepath.Join(ycp.installBase, "yba-ctl")
}

// LogFile is the yba-ctl log file path.
func (ycp ybaCtlPaths) LogFile() string {
	return filepath.Join(ycp.InstallDir(), "yba-ctl.log")
}

// InputFile is the config file yba-ctl uses.
func (ycp ybaCtlPaths) InputFile() string {
	return filepath.Join(ycp.InstallDir(), "yba-ctl.yml")
}

// LicenseFile is the path to the license file.
func (ycp ybaCtlPaths) LicenseFile() string {
	return filepath.Join(ycp.InstallDir(), "yba.lic")
}

// YbaInstallingMarker gets the .installing marker file, used to track
// when a yba install is in progress.
func (ycp ybaCtlPaths) YbaInstallingMarker() string {
	return filepath.Join(ycp.InstallDir(), ".installing")
}

// YbaInstallingMarker gets the .installed marker file, used to track
// when a yba install is complete.
func (ycp ybaCtlPaths) YbaInstalledMarker() string {
	return filepath.Join(ycp.InstallDir(), ".installed")
}
