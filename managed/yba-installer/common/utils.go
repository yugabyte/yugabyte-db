/*
* Copyright (c) YugaByte, Inc.
 */

package common

import (
	"bufio"
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

// Hardcoded Variables.

// Systemctl linux command.
const Systemctl string = "systemctl"

// InputFile where installer config settings are specified.
var InputFile = "/opt/yba-ctl/yba-ctl.yml"

var installingFile = "/opt/yba-ctl/.installing"

// InstalledFile is location of install completed marker file.
var InstalledFile = "/opt/yba-ctl/.installed"

// BundledPostgresName is postgres package we ship with yba_installer_full.
var BundledPostgresName = "postgresql-9.6.24-1-linux-x64-binaries.tar.gz"

var skipConfirmation = false

var yumList = []string{"RedHat", "CentOS", "Oracle", "Alma", "Amazon"}

var aptList = []string{"Ubuntu", "Debian"}

var goBinaryName = "yba-ctl"

var versionMetadataJSON = "version_metadata.json"

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
	ex, err := os.Executable()
	if err != nil {
		panic(err)
	}
	exPath := filepath.Dir(ex)

	var configViper = viper.New()
	configViper.SetConfigName("version_metadata.json")
	configViper.SetConfigType("json")
	configViper.AddConfigPath(exPath)

	err = configViper.ReadInConfig()
	if err != nil {
		panic(err)
	}

	versionNumber := fmt.Sprint(configViper.Get("version_number"))
	buildNumber := fmt.Sprint(configViper.Get("build_number"))

	version := versionNumber + "-b" + buildNumber

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

// Contains checks an array values for the presence of target.
// Type must be a comparable
func Contains[T comparable](values []T, target T) bool {
	for _, v := range values {
		if v == target {
			return true
		}
	}
	return false
}

// Chown changes ownership of dir to user:group, recursively (optional).
func Chown(dir, user, group string, recursive bool) error {
	args := []string{fmt.Sprintf("%s:%s", user, group), dir}
	if recursive {
		args = append([]string{"-R"}, args...)
	}
	_, err := ExecuteBashCommand("chown", args)
	return err
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

type defaultAnswer int

func (d defaultAnswer) String() string {
	return strconv.Itoa(int(d))
}

const (
	DefaultNone defaultAnswer = iota
	DefaultYes
	DefaultNo
)

// DisableUserConfirm skips all confirmation steps.
func DisableUserConfirm() {
	skipConfirmation = true
}

// UserConfirm asks the user for confirmation before proceeding.
func UserConfirm(prompt string, defAns defaultAnswer) bool {
	if skipConfirmation {
		return true
	}
	if !strings.HasSuffix(prompt, " ") {
		prompt = prompt + " "
	}
	var selector string
	switch defAns {
	case DefaultNone:
		selector = "[yes/no]"
	case DefaultYes:
		selector = "[YES/no]"
	case DefaultNo:
		selector = "[yes/NO]"
	default:
		log.Fatal("unknown defaultanswer " + defAns.String())
	}

	for {
		fmt.Printf("%s%s: ", prompt, selector)
		reader := bufio.NewReader(os.Stdin)
		input, err := reader.ReadString('\n')
		if err != nil {
			fmt.Println("invalid input: " + err.Error())
			continue
		}
		input = strings.TrimSuffix(input, "\n")
		input = strings.Trim(input, " ")
		input = strings.ToLower(input)
		switch input {
		case "n", "no":
			return false
		case "y", "yes":
			return true
		case "":
			if defAns == DefaultYes {
				return true
			} else if defAns == DefaultNo {
				return false
			}
			fallthrough
		default:
			fmt.Println("please enter 'yes' or 'no'")
		}
	}
}

func InitViper() {
	// Init Viper
	viper.SetDefault("service_username", "yugabyte")
	viper.SetDefault("installRoot", "/opt/yugabyte")
	viper.SetConfigFile(InputFile)
	viper.ReadInConfig()
}

func GetBinaryDir() string {

	ex, err := os.Executable()
	if err != nil {
		log.Fatal("Error determining yba-ctl binary path.")
	}
	return filepath.Dir(ex)
}

func GetReferenceYaml() string {
	return filepath.Join(GetBinaryDir(), "yba-ctl.yml.reference")
}

func init() {
	InitViper()
	// Init globals that rely on viper

	/*
		Version = GetVersion()
		InstallRoot = GetInstallRoot()
		InstallVersionDir = GetInstallVersionDir()
		yugabundleBinary = "yugabundle-" + Version + "-centos-x86_64.tar.gz"
		currentUser = GetCurrentUser()
	*/
}
