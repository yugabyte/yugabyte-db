/*
* Copyright (c) YugaByte, Inc.
 */

package cmd

import (
	"bufio"
	"bytes"
	"crypto/rand"
	"encoding/base64"
	"fmt"
	"io/ioutil"
	"os"
	"os/exec"
	"os/user"
	"path/filepath"
	"strconv"
	"strings"

	log "github.com/sirupsen/logrus"
)

// Bash Command Constants
// SYSTEMCTL linux command
const SYSTEMCTL string = "systemctl"
// LN linux command
const LN string = "ln"
// CHOWN linux command
const CHOWN string = "chown"
// SYSTEMD_DIR Systemd service file directory
const SYSTEMD_DIR string = "/etc/systemd/system"

func ExecuteBashCommand(command string, args []string) (o string, e error) {

	LogDebug("Running command " + command + " " + strings.Join(args, " "))
	cmd := exec.Command(command, args...)

	var execOut bytes.Buffer
	var execErr bytes.Buffer
	cmd.Stdout = &execOut
	cmd.Stderr = &execErr

	err := cmd.Run()

	if err == nil {
		LogDebug(command + " " + strings.Join(args, " ") + " successfully executed.")
	} else {
		LogDebug(command + " " + strings.Join(args, " ") + " failed with error " + err.Error() +
			"\nPrinting stdOut/stdErr " + execOut.String() + execErr.String())
	}

	return execOut.String(), err
}

func IndexOf(arr []string, val string) int {

	for pos, v := range arr {
		if v == val {
			return pos
		}
	}

	return -1
}

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
	ExecuteBashCommand(CHOWN, args)
}

func YumInstall(args []string) {

	argsFull := append([]string{"-y", "install"}, args...)
	ExecuteBashCommand("yum", argsFull)
}

func FirewallCmdEnable(args []string) {

	argsFull := append([]string{"--zone=public", "--permanent"}, args...)
	ExecuteBashCommand("firewall-cmd", argsFull)
}

// Utility method as to whether or not the user has sudo access and is running
// the program as root.
func hasSudoAccess() bool {

	cmd := exec.Command("id", "-u")
	output, err := cmd.Output()
	if err != nil {
		LogError("Error: " + err.Error() + ".")
	}

	i, err := strconv.Atoi(string(output[:len(output)-1]))
	if err != nil {
		LogError("Error: " + err.Error() + ".")
	}

	if i == 0 {
		return true
	} else {
		return false
	}
}

func containsSubstring(s []string, str string) bool {

	for _, v := range s {

		if strings.Contains(str, v) {
			return true
		}
	}
	return false
}

func TestSudoPermission() {

	cmd := exec.Command("id", "-u")
	output, err := cmd.Output()
	if err != nil {
		LogError("Error: " + err.Error() + ".")
	}

	i, err := strconv.Atoi(string(output[:len(output)-1]))
	if err != nil {
		LogError("Error: " + err.Error() + ".")
	}

	if i == 0 {
		LogDebug("Awesome! You are now running this program with root permissions.")
	} else {
		LogDebug("You are not running this program with root permissions. " +
			"Executing Preflight Root Checks.")
		PreflightRoot()
	}
}

func GetInstallRoot() string {

	InstallRoot := "/opt/yugabyte"

	if !hasSudoAccess() {
		InstallRoot = "/home/" + currentUser + "/yugabyte"
	}

	return InstallRoot

}

func GetInstallVersionDir() string {

	return GetInstallRoot() + "/yba_installer-" + version
}

func GetCurrentUser() string {
	user, err := user.Current()
	if err != nil {
		LogError(fmt.Sprintf("Error %s getting current user", err.Error()))
	}
	return user.Username
}

func GenerateRandomBytes(n int) ([]byte, error) {

	b := make([]byte, n)
	_, err := rand.Read(b)
	if err != nil {
		return nil, err
	}
	return b, nil
}

// GenerateRandomStringURLSafe is used to generate the PlatformAppSecret.
func GenerateRandomStringURLSafe(n int) string {

	b, _ := GenerateRandomBytes(n)
	return base64.URLEncoding.EncodeToString(b)
}

func ReplaceTextGolang(fileName string, textToReplace string, textToReplaceWith string) {

	input, err := ioutil.ReadFile(fileName)
	if err != nil {
		LogError("Error: " + err.Error() + ".")

	}

	output := bytes.Replace(input, []byte(textToReplace), []byte(textToReplaceWith), -1)
	if err = ioutil.WriteFile(fileName, output, 0666); err != nil {
		LogError("Error: " + err.Error() + ".")

	}
}

func WriteTextIfNotExistsGolang(fileName string, textToWrite string) {

	byteFileService, errRead := ioutil.ReadFile(fileName)
	if errRead != nil {
		LogError("Error: " + errRead.Error() + ".")
	}

	stringFileService := string(byteFileService)
	if !strings.Contains(stringFileService, textToWrite) {
		f, _ := os.OpenFile(fileName, os.O_APPEND|os.O_WRONLY, 0644)
		f.WriteString(textToWrite)
		f.Close()
	}
}

// CopyFileGolang copies src file to dst.
// Assumes both src/dst are valid absolute paths and dst file parent directory is already created.
func CopyFileGolang(src string, dst string) {

	bytesRead, errSrc := ioutil.ReadFile(src)

	if errSrc != nil {
		LogError("Error: " + errSrc.Error() + ".")
	}
	errDst := ioutil.WriteFile(dst, bytesRead, 0644)
	if errDst != nil {
		LogError("Error: " + errDst.Error() + ".")
	}

	LogDebug("Copy from " + src + " to " + dst + " executed successfully.")
}

// CreateDir creates a directory according to the given permissions, logging an error if necessary.
func CreateDir(dir string, perm os.FileMode) {
	err := os.MkdirAll(dir, perm)
	if err != nil && !os.IsExist(err) {
		LogError(fmt.Sprintf("Error creating %s. Failed with %s", dir, err.Error()))
	}
}

func MoveFileGolang(src string, dst string) {

	err := os.Rename(src, dst)
	if err != nil {
		LogError("Error: " + err.Error() + ".")
	}

	LogDebug("Move from " + src + " to " + dst + " executed successfully.")

}

func GenerateCORSOrigin() string {

	command0 := "bash"
	args0 := []string{"-c", "ip route get 1.2.3.4 | awk '{print $7}'"}
	cmd := exec.Command(command0, args0...)
	cmd.Stderr = os.Stderr
	out, _ := cmd.Output()
	CORSOriginIP := string(out)
	CORSOrigin := "https://" + strings.TrimSuffix(CORSOriginIP, "\n") + ""
	return strings.TrimSuffix(strings.ReplaceAll(CORSOrigin, " ", ""), "\n")
}

func WriteToWhitelist(command string, args []string) {

	_, err := ExecuteBashCommand(command, args)
	if err != nil {
		LogError("Error: " + err.Error() + ".")
	} else {
		LogDebug(args[1] + " executed.")
	}
}

func AddWhitelistRuleIfNotExists(rule string) {

	byteSudoers, errRead := ioutil.ReadFile("/etc/sudoers")
	if errRead != nil {
		LogError("Error: " + errRead.Error() + ".")
	}

	stringSudoers := string(byteSudoers)

	if !strings.Contains(stringSudoers, rule) {
		command := "bash"
		argItem := "echo " + "'" + rule + "' | sudo EDITOR='tee -a' visudo"
		argList := []string{"-c", argItem}
		WriteToWhitelist(command, argList)
	}
}

func SetUpSudoWhiteList() {

	whitelistFile, err := os.Open("whitelistRules.txt")
	if err != nil {
		LogError("Error: " + err.Error() + ".")
	}

	whitelistFileScanner := bufio.NewScanner(whitelistFile)
	whitelistFileScanner.Split(bufio.ScanLines)
	var whitelistRules []string
	for whitelistFileScanner.Scan() {
		whitelistRules = append(whitelistRules, whitelistFileScanner.Text())
	}

	whitelistFile.Close()
	for _, rule := range whitelistRules {
		AddWhitelistRuleIfNotExists(rule)
	}
}

// Create a file at a relative path for the non-root case. Have to make the directory before
// inserting the file in that directory.
func Create(p string) (*os.File, error) {
	if err := os.MkdirAll(filepath.Dir(p), 0777); err != nil {
		LogError(fmt.Sprintf("Error creating %s. Failed with %s", p, err.Error()))
		return nil, err
	}
	return os.Create(p)
}

func validRelPath(p string) bool {
	if p == "" || strings.Contains(p, `\`) || strings.HasPrefix(p, "/") || strings.Contains(p, "../") {
		return false
	}
	return true
}

// LogError prints the error message to stdout at the error level, and
// then kills the currently running process.
func LogError(errorMsg string) {
	log.Fatalln(errorMsg)
}

// LogInfo prints the info message to the console at the info level.
func LogInfo(infoMsg string) {
	log.Infoln(infoMsg)
}

// LogDebug prints the debug message to the console at the debug level.
func LogDebug(debugMsg string) {
	log.Debugln(debugMsg)
}

func ValidateArgLength(command string, args []string, minValidArgs int, maxValidArgs int) {

	if minValidArgs != -1 && maxValidArgs != -1 {
		if len(args) < minValidArgs || len(args) > maxValidArgs {
			LogInfo("Invalid provided arguments: " + strings.Join(args, " ") + ".")
			if minValidArgs == maxValidArgs {
				LogError("The subcommand " + command + " only takes exactly " + strconv.Itoa(minValidArgs) +
					" argument.")
			} else {
				LogError("The subcommand " + command + " only takes between " + strconv.Itoa(minValidArgs) +
					" and " + strconv.Itoa(maxValidArgs) + " arguments.")
			}
		}

	} else if maxValidArgs != -1 {
		if len(args) > maxValidArgs {
			LogInfo("Invalid provided arguments: " + strings.Join(args, " ") + ".")
			LogError("The subcommand " + command + " only takes up to " + strconv.Itoa(maxValidArgs) +
				" arguments.")
		}

	} else if minValidArgs != -1 {
		if len(args) < minValidArgs {
			LogInfo("Invalid provided arguments: " + strings.Join(args, " ") + ".")
			LogError("The subcommand " + command + " only takes at least " + strconv.Itoa(minValidArgs) +
				" arguments.")
		}
	}
}

func ExactValidateArgLength(command string, args []string, exactArgLength []int) {

	var validOption = false
	for _, length := range exactArgLength {
		if len(args) == length {
			validOption = true
			break
		}
	}
	if !validOption {
		var lengths []string
		for _, i := range exactArgLength {
			lengths = append(lengths, strconv.Itoa(i))
		}
		LogInfo("Invalid provided arguments: " + strings.Join(args, " ") + ".")
		LogError("The subcommand " + command + " only takes in any of the following number" +
			" of arguments: " + strings.Join(lengths, " ") + ".")
	}
}
