/*
* Copyright (c) YugaByte, Inc.
 */

package common

import (
	"bytes"
	"fmt"
	"os"
	"os/exec"
	"os/user"
	"path/filepath"
	"strconv"
	"strings"

	"github.com/spf13/viper"

	log "github.com/yugabyte/yugabyte-db/managed/yba-installer/logging"
	// "github.com/yugabyte/yugabyte-db/managed/yba-installer/preflight"
)

// Bash Command Constants

// Systemctl linux command.
const Systemctl string = "systemctl"

// SystemdDir service file directory.
const SystemdDir string = "/etc/systemd/system"

// InstallRoot where YBA is installed.
var InstallRoot = GetInstallRoot()

// InstallVersionDir where the yba_installer directory is.
var InstallVersionDir = InstallRoot + "/yba_installer-" + GetVersion()

// InputFile where installer config settings are specified.
var InputFile = "yba-installer-input.yml"

// BundledPostgresName is postgres package we ship with yba_installer_full.
var BundledPostgresName = "postgresql-9.6.24-1-linux-x64-binaries.tar.gz"

// ConfigDir is directory where service config file templates are stored (relative to yba-ctl)
var ConfigDir = "templates"

// CronDir is directory where non-root cron scripts are stored (relative to yba-ctl)
var CronDir = "cron"

var yumList = []string{"RedHat", "CentOS", "Oracle", "Alma", "Amazon"}

var aptList = []string{"Ubuntu", "Debian"}

var currentUser = GetCurrentUser()

var goBinaryName = "yba-ctl"

var versionMetadataJSON = "version_metadata.json"

var yugabundleBinary = "yugabundle-" + GetVersion() + "-centos-x86_64.tar.gz"

var javaBinaryName = "OpenJDK8U-jdk_x64_linux_hotspot_8u345b01.tar.gz"

var pemToKeystoreConverter = "pemtokeystore-linux-amd64"

// DetectOS detects the operating system yba-installer is running on.
func DetectOS() string {

	command1 := "bash"
	args1 := []string{"-c", "awk -F= '/^NAME/{print $2}' /etc/os-release"}
	output, _ := ExecuteBashCommand(command1, args1)

	return string(output)
}

// GetVersion gets the version at execution time so that yba-installer
// installs the correct version of Yugabyte Anywhere.
func GetVersion() string {

	cwd, _ := os.Getwd()
	currentFolderPathList := strings.Split(cwd, "/")
	lenFolder := len(currentFolderPathList)
	currFolder := currentFolderPathList[lenFolder-1]

	versionInformation := strings.Split(currFolder, "-")

	// In case we are not executing in the install directory, return
	// the version present in versionMetadata.json.
	if len(versionInformation) < 3 {

		viper.SetConfigName("version_metadata.json")
		viper.SetConfigType("json")
		viper.AddConfigPath(".")
		err := viper.ReadInConfig()
		if err != nil {
			panic(err)
		}

		versionNumber := fmt.Sprint(viper.Get("version_number"))
		buildNumber := fmt.Sprint(viper.Get("build_number"))

		version := versionNumber + "-" + buildNumber

		return version

	}

	versionNumber := versionInformation[1]
	buildNumber := versionInformation[2]

	version := versionNumber + "-" + buildNumber

	return version
}

// ExecuteBashCommand exeuctes a command in the shell, returning the output and error.
func ExecuteBashCommand(command string, args []string) (o string, e error) {

	log.Debug("Running command " + command + " " + strings.Join(args, " "))
	cmd := exec.Command(command, args...)

	var execOut bytes.Buffer
	var execErr bytes.Buffer
	cmd.Stdout = &execOut
	cmd.Stderr = &execErr

	err := cmd.Run()

	if err == nil {
		log.Debug(command + " " + strings.Join(args, " ") + " successfully executed.")
	} else {
		log.Info("ERROR: '" + command + " " + strings.Join(args, " ") + "' failed with error " +
			err.Error() + "\nPrinting stdOut/stdErr " + execOut.String() + execErr.String())
	}

	return execOut.String(), err
}

// IndexOf returns the index in arr where val is present, -1 otherwise.
func IndexOf(arr []string, val string) int {

	for pos, v := range arr {
		if v == val {
			return pos
		}
	}

	return -1
}

// Contains checks an array s for the presence of str.
func Contains(s []string, str string) bool {

	for _, v := range s {

		if v == str {
			return true
		}
	}
	return false
}

// Chown changes ownership of dir to user:group, recursively (optional).
func Chown(dir, user, group string, recursive bool) {
	args := []string{fmt.Sprintf("%s:%s", user, group), dir}
	if recursive {
		args = append([]string{"-R"}, args...)
	}
	ExecuteBashCommand("chown", args)
}

// HasSudoAccess determines whether or not running user has sudo permissions.
func HasSudoAccess() bool {

	cmd := exec.Command("id", "-u")
	output, err := cmd.Output()
	if err != nil {
		log.Fatal("Error: " + err.Error() + ".")
	}

	i, err := strconv.Atoi(string(output[:len(output)-1]))
	if err != nil {
		log.Fatal("Error: " + err.Error() + ".")
	}

	if i == 0 {
		return true
	}
	return false
}

// GetInstallRoot returns the InstallRoot where YBA is installed.
func GetInstallRoot() string {

	InstallRoot := "/opt/yugabyte"

	if !HasSudoAccess() {
		InstallRoot = "/home/" + currentUser + "/yugabyte"
	}

	return InstallRoot

}

// GetInstallVersionDir returns the yba_installer directory inside InstallRoot
func GetInstallVersionDir() string {

	return GetInstallRoot() + "/yba_installer-" + GetVersion()
}

// GetCurrentUser returns the user yba-ctl was run as.
func GetCurrentUser() string {
	user, err := user.Current()
	if err != nil {
		log.Fatal(fmt.Sprintf("Error %s getting current user", err.Error()))
	}
	return user.Username
}

// CopyFileGolang copies src file to dst.
// Assumes both src/dst are valid absolute paths and dst file parent directory is already created.
func CopyFileGolang(src string, dst string) {

	bytesRead, errSrc := os.ReadFile(src)

	if errSrc != nil {
		log.Fatal("Error: " + errSrc.Error() + ".")
	}
	errDst := os.WriteFile(dst, bytesRead, 0644)
	if errDst != nil {
		log.Fatal("Error: " + errDst.Error() + ".")
	}

	log.Debug("Copy from " + src + " to " + dst + " executed successfully.")
}

// CreateDir creates a directory according to the given permissions, logging an error if necessary.
func CreateDir(dir string, perm os.FileMode) {
	err := os.MkdirAll(dir, perm)
	if err != nil && !os.IsExist(err) {
		log.Fatal(fmt.Sprintf("Error creating %s. Failed with %s", dir, err.Error()))
	}
}

// MoveFileGolang moves (renames) a src file to dst.
func MoveFileGolang(src string, dst string) {

	err := os.Rename(src, dst)
	if err != nil {
		log.Fatal("Error: " + err.Error() + ".")
	}

	log.Debug("Move from " + src + " to " + dst + " executed successfully.")

}

// Create a file at a relative path for the non-root case. Have to make the directory before
// inserting the file in that directory.
func Create(p string) (*os.File, error) {
	if err := os.MkdirAll(filepath.Dir(p), 0777); err != nil {
		log.Fatal(fmt.Sprintf("Error creating %s. Failed with %s", p, err.Error()))
		return nil, err
	}
	return os.Create(p)
}

// CreateSymlink of binary from pkgDir to linkDir.
/*
	pkgDir - directory where the binary (file or directory) is located
	linkDir - directory where you want the link to be created
	binary - name of file or directory to link. should already exist in pkgDir and will be the same
*/
func CreateSymlink(pkgDir string, linkDir string, binary string) {
	binaryPath := fmt.Sprintf("%s/%s", pkgDir, binary)
	linkPath := fmt.Sprintf("%s/%s", linkDir, binary)

	args := []string{"-sf", binaryPath, linkPath}
	ExecuteBashCommand("ln", args)
}
