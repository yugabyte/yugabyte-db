package common

import (
	"fmt"
	"io/fs"
	"io/ioutil"
	"os"
	"path/filepath"
	"sort"

	"github.com/spf13/viper"
	log "github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/logging"
)

/* Directory Structure for Yugabyte Installs:
*
* A default install will be laid out roughly as follows
* /opt/yugabyte/
*               data/
*                    logs/
                     postgres/
					 pgsql/
					 prometheus/
					 yb-platform/
					 ybdb/
*               software/
*                  2.16.1.0-b234/
                   2.18.2.0-b12/

* Base Install:   /opt/yugabyte
* Install root:   /opt/yugabyte/software/2.18.2.0-b12/
* Data directory: /opt/yugabyte/data
* Active Symlink: /opt/yugabyte/software/active
*
# GetBaseInstall will return the installRoot
*/

// ALl of our install files and directories.
const (
	InstallSymlink string = "active"

	dataVersionFileName = ".version.txt"
)

// Directory names for config files
const (
	ConfigDir = "templates" // directory name service config templates are (relative to yba-ctl)
)

// SystemdDir service file directory.
var SystemdDir string = "/etc/systemd/system"

// Version of yba-ctl
var Version = ""

// YbactlVersion returns the version of yba-ctl
func YbactlVersion() string {
	if Version != "" {
		return Version
	}
	log.Fatal("Version unset in directory manager.")
	return ""
}

// GetBaseInstall returns the base install directory, as defined by the user
func GetBaseInstall() string {
	return dm.BaseInstall()
}

func GetDataRoot() string {
	return filepath.Join(dm.BaseInstall(), "data")
}

func DataVersionFile() string {
	return filepath.Join(GetDataRoot(), dataVersionFileName)
}

// GetSoftwareRoot returns the version specific directory inside of installRoot/software
func GetSoftwareRoot() string {
	return dm.WorkingDirectory()
}

// GetSoftwareDir returns the path to the 'software' directory.
func GetSoftwareDir() string {
	return filepath.Join(dm.BaseInstall(), "software")
}

// GetActiveSymlink will return the symlink file name
func GetActiveSymlink() string {
	return dm.ActiveSymlink()
}

// GetInstallerSoftwareDir returns the yba_installer directory inside InstallRoot
func GetInstallerSoftwareDir() string {
	return dm.WorkingDirectory() + "/yba_installer"
}

func PrunePastInstalls() {
	softwareRoot := filepath.Join(dm.BaseInstall(), "software")
	entries, err := ioutil.ReadDir(softwareRoot)
	if err != nil {
		log.Fatal(err.Error())
	}

	activePath, err := filepath.EvalSymlinks(GetActiveSymlink())
	if err != nil {
		log.Fatal(err.Error())
	}
	activePathBase := filepath.Base(activePath)

	log.Debug(fmt.Sprintf("List before prune1"))
	for _, entry := range entries {
		log.Debug("Entry before prune1 " + entry.Name())
	}

	versionEntries := FilterList[fs.FileInfo](
		entries,
		func(f fs.FileInfo) bool {
			return IsValidVersion(f.Name()) && f.Name() != activePathBase
		})
	sort.Slice(
		versionEntries,
		func(e1, e2 int) bool {
			return LessVersions(versionEntries[e1].Name(), versionEntries[e2].Name())
		},
	)

	// versionEntries has all older releases at this point
	log.Debug(fmt.Sprintf("List before prune2"))
	for _, entry := range versionEntries {
		log.Debug("Entry before prune2 " + entry.Name())
	}
	// only keep one old release
	for i := 0; i < len(versionEntries)-1; i++ {
		toDel := filepath.Join(softwareRoot, versionEntries[i].Name())
		log.Warn(fmt.Sprintf("Removing old release directory %s", toDel))
		RemoveAll(toDel)
	}

}

// Default the directory manager to using the install workflow.
var dm directoryManager = directoryManager{
	Workflow:          workflowInstall,
	replicatedBaseDir: "/opt/yugabyte", // default replicated install directory
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
	Workflow          workflow
	replicatedBaseDir string
}

func (dm directoryManager) BaseInstall() string {
	return viper.GetString("installRoot")
}

// WorkingDirectory returns the directory the workflow should be using
// the active directory for install case, and the inactive for upgrade case.
func (dm directoryManager) WorkingDirectory() string {

	return filepath.Join(dm.BaseInstall(), "software", YbactlVersion())
}

// GetActiveSymlink will return the symlink file name
func (dm directoryManager) ActiveSymlink() string {
	return filepath.Join(dm.BaseInstall(), "software", InstallSymlink)
}

func (dm directoryManager) ReplicatedBaseDir() string {
	return dm.replicatedBaseDir
}

func (dm directoryManager) SetReplicatedBaseDir(dir string) {
	dm.replicatedBaseDir = dir
}

func GetPostgresPackagePath() string {
	return GetFileMatchingGlobOrFatal(PostgresPackageGlob)
}

func GetJavaPackagePath() string {
	return GetFileMatchingGlobOrFatal(javaBinaryGlob)
}

// Returns YBDB package path.
func GetYbdbPackagePath() string {
	return GetFileMatchingGlobOrFatal(ybdbPackageGlob)
}

// Gets 0 or 1 matches of YBDB package path.
// Fatal error if more than 1 match.
func MaybeGetYbdbPackagePath() string {
	path, matches, err := GetFileMatchingGlob(ybdbPackageGlob)
	// Fatal if more than one match found.
	if err != nil && matches > 1 {
		log.Fatal(err.Error())
	}
	return path
}

func GetTemplatesDir() string {
	// if we are being run from the installed dir, templates
	// is in the same dir as the binary
	installedPath := filepath.Join(GetInstallerSoftwareDir(), ConfigDir)
	if _, err := os.Stat(installedPath); err == nil {
		return installedPath
	}

	// if we are being run from the .tar.gz before install
	return GetFileMatchingGlobOrFatal(filepath.Join(GetBinaryDir(), tarTemplateDirGlob))
}

func GetSelfSignedServerCertPath() string {
	return filepath.Join(GetSelfSignedCertsDir(), ServerCertPath)
}

func GetSelfSignedServerKeyPath() string {
	return filepath.Join(GetSelfSignedCertsDir(), ServerKeyPath)
}

func GetSelfSignedCACertPath() string {
	return filepath.Join(GetSelfSignedCertsDir(), "ca_cert.pem")
}

func GetSelfSignedCAKeyPath() string {
	return filepath.Join(GetSelfSignedCertsDir(), "ca_key.pem")
}

func GetYBAInstallerDataDir() string {
	return filepath.Join(GetDataRoot(), "yba-installer")
}
func GetSelfSignedCertsDir() string {
	return filepath.Join(GetYBAInstallerDataDir(), "certs")
}

// GetPerfAdvisorBaseDataDir returns the perf-advisor directory under baseInstall (config, certs, etc.).
func GetPerfAdvisorBaseDataDir() string {
	return filepath.Join(GetBaseInstall(), "perf-advisor")
}

// GetPerfAdvisorCertsDir returns the directory for Perf Advisor TLS certs (e.g. tls.p12).
func GetPerfAdvisorCertsDir() string {
	return filepath.Join(GetPerfAdvisorBaseDataDir(), "certs")
}

// GetPlatformServerCertPaths returns the paths to the platform server cert and key (YBA TLS).
// Uses viper server_cert_path/server_key_path when set, otherwise self-signed cert paths.
func GetPlatformServerCertPaths() (certPath, keyPath string) {
	if k := viper.GetString("server_key_path"); k != "" {
		return viper.GetString("server_cert_path"), k
	}
	return GetSelfSignedServerCertPath(), GetSelfSignedServerKeyPath()
}

func GetReplicatedBaseDir() string {
	return dm.ReplicatedBaseDir()
}

func SetReplicatedBaseDir(dir string) {
	dm.replicatedBaseDir = dir
}
