/*
* Copyright (c) YugaByte, Inc.
 */

package main

import (
	"archive/tar"
	"bufio"
	"bytes"
	"compress/gzip"
	"crypto/rand"
	"encoding/base64"
	"errors"
	"fmt"
	log "github.com/sirupsen/logrus"
	"io"
	"io/fs"
	"io/ioutil"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"strconv"
	"strings"
	"time"
)

func ExecuteBashCommand(command string, args []string) (o string, e error) {

	cmd := exec.Command(command, args...)

	var execOut bytes.Buffer
	var execErr bytes.Buffer
	cmd.Stdout = &execOut
	cmd.Stderr = &execErr

	err := cmd.Run()

	if err == nil {
		LogDebug(command + " " + strings.Join(args, " ") + " successfully executed.")
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

	return GetInstallRoot() + "/yba_installer-" + GetVersion()
}

func GetCurrentUser() string {
	currentUser, _ := ExecuteBashCommand("bash", []string{"-c", "whoami"})
	currentUser = strings.ReplaceAll(strings.TrimSuffix(currentUser, "\n"), " ", "")
	return currentUser
}

func GenerateRandomBytes(n int) ([]byte, error) {

	b := make([]byte, n)
	_, err := rand.Read(b)
	if err != nil {
		return nil, err
	}
	return b, nil
}

//GenerateRandomStringURLSafe is used to generate the PlatformAppSecret.
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
		return nil, err
	}
	return os.Create(p)
}

// Untar reads the gzip-compressed tar file from r and writes it into dir.
func Untar(r io.Reader, dir string) error {
	return untar(r, dir)
}

func untar(r io.Reader, dir string) (err error) {
	t0 := time.Now()
	nFiles := 0
	madeDir := map[string]bool{}
	defer func() {
		td := time.Since(t0)
		if err == nil {
			LogDebug(fmt.Sprintf("Extracted tarball into %s: %d files, %d dirs (%v).",
				dir, nFiles, len(madeDir), td))
		} else {
			LogDebug(fmt.Sprintf("Error extracting tarball into %s after %d files, %d dirs,"+
				"%v: %v.", dir, nFiles, len(madeDir), td, err))
		}
	}()
	zr, err := gzip.NewReader(r)
	if err != nil {
		return fmt.Errorf("Requires gzip-compressed body: %v.", err)
	}
	tr := tar.NewReader(zr)
	loggedChtimesError := false
	for {
		f, err := tr.Next()
		if err == io.EOF {
			break
		}
		if err != nil {
			LogDebug(fmt.Sprintf("Tar reading error: %v.", err))
			return fmt.Errorf("Tar error: %v.", err)
		}
		if !validRelPath(f.Name) {
			return fmt.Errorf("Tar contained invalid name error %q.", f.Name)
		}
		rel := filepath.FromSlash(f.Name)
		abs := filepath.Join(dir, rel)

		fi := f.FileInfo()
		mode := fi.Mode()
		switch {
		case mode.IsRegular():
			// Make the directory. This is redundant because it should
			// already be made by a directory entry in the tar
			// beforehand. Thus, don't check for errors; the next
			// write will fail with the same error.
			dir := filepath.Dir(abs)
			if !madeDir[dir] {
				if err := os.MkdirAll(filepath.Dir(abs), 0755); err != nil {
					return err
				}
				madeDir[dir] = true
			}
			if runtime.GOOS == "darwin" && mode&0111 != 0 {
				// The darwin kernel caches binary signatures
				// and SIGKILLs binaries with mismatched
				// signatures. Overwriting a binary with
				// O_TRUNC does not clear the cache, rendering
				// the new copy unusable. Removing the original
				// file first does clear the cache. See #54132.
				err := os.Remove(abs)
				if err != nil && !errors.Is(err, fs.ErrNotExist) {
					return err
				}
			}
			wf, err := os.OpenFile(abs, os.O_RDWR|os.O_CREATE|os.O_TRUNC, mode.Perm())
			if err != nil {
				return err
			}
			n, err := io.Copy(wf, tr)
			if closeErr := wf.Close(); closeErr != nil && err == nil {
				err = closeErr
			}
			if err != nil {
				return fmt.Errorf("Error writing to %s: %v.", abs, err)
			}
			if n != f.Size {
				return fmt.Errorf("Only wrote %d bytes to %s; expected %d.", n, abs, f.Size)
			}
			modTime := f.ModTime
			if modTime.After(t0) {
				// Clamp modtimes at system time. See
				// golang.org/issue/19062 when clock on
				// buildlet was behind the gitmirror server
				// doing the git-archive.
				modTime = t0
			}
			if !modTime.IsZero() {
				if err := os.Chtimes(abs, modTime, modTime); err != nil && !loggedChtimesError {
					// benign error. Gerrit doesn't even set the
					// modtime in these, and we don't end up relying
					// on it anywhere (the gomote push command relies
					// on digests only), so this is a little pointless
					// for now.
					LogDebug(fmt.Sprintf("Error changing modtime: %v "+
						"(further Chtimes errors suppressed).", err))
					loggedChtimesError = true // once is enough
				}
			}
			nFiles++
		case mode.IsDir():
			if err := os.MkdirAll(abs, 0755); err != nil {
				return err
			}
			madeDir[abs] = true
		default:
			return fmt.Errorf("Tar file entry %s contained unsupported file type %v.", f.Name, mode)
		}
	}
	return nil
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
