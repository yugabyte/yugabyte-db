/*
 * Copyright (c) YugaByte, Inc.
 */

package preflight

 import (
    log "github.com/sirupsen/logrus"
    "os/exec"
    "bytes"
    "strings"
	"strconv"
 )

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

func GetInstallRoot() (string) {

    InstallRoot := "/opt/yugabyte"

    if ! hasSudoAccess() {
        InstallRoot = "/home/" + currentUser + "/yugabyte"
    }

    return InstallRoot

}

func GetCurrentUser() (string) {
    currentUser, _ := ExecuteBashCommand("bash", []string{"-c", "whoami"})
    currentUser = strings.ReplaceAll(strings.TrimSuffix(currentUser, "\n"), " ", "")
    return currentUser
}

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

// Utility method as to whether or not the user has sudo access and is running
// the program as root.
func hasSudoAccess() (bool) {

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