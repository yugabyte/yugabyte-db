/*
* Copyright (c) YugaByte, Inc.
*/

package main

import (
"log"
"strings"
)

// Actions that require root for us to execute are not possible under a non-root
// installation. We will simply add them to the preflight checks so that the user
// will know to perform these necessary installs prior to running the install command
// for Yba-installer themselves.

func checkPythonInstalled() {

    command := "bash"
    args := []string{"-c", "command -v python3"}
    output, _ := ExecuteBashCommand(command, args)

    outputTrimmed := strings.Replace(output, "\r\n", "", -1)

    if outputTrimmed == "" {

    log.Fatal("Python 3 not installed on the system, " +
    "please install Python 3 before continuing. As an example, " +
    "CentOS users can invoke the command \"sudo yum install -y " +
    "python3\"" + " in order to do so.")

    }

}

func checkPython3PipInstalled() {

    command := "bash"
    args := []string{"-c", "pip3 --version"}
    output, _ := ExecuteBashCommand(command, args)

    outputTrimmed := strings.Replace(output, "\r\n", "", -1)

    if strings.Contains(outputTrimmed, "command not found") {

        log.Fatal("Python 3 pip not installed on the system, " +
        "please install Python 3 pip before continuing. As an example, " +
        "CentOS users can invoke the command \"sudo yum -y install " +
        "python3-pip\" in order to do so.")

    }

}

func checkJavaInstalled() {

    command := "bash"
    args := []string{"-c", "command -v java"}
    output, _ := ExecuteBashCommand(command, args)

    outputTrimmed := strings.Replace(output, "\r\n", "", -1)

    if outputTrimmed == "" {

        log.Fatal("Java 8 not installed on the system, " +
        "please install Java 8 before continuing. As an example, " +
        "CentOS users can invoke the command \"sudo yum -y install " +
        "java-1.8.0-openjdk java-1.8.0-openjdk-devel\"" + " in order " +
        "to do so.")
    }

}

func checkUserLevelSystemdCapable() {

    if strings.Contains(DetectOS(), "CentOS") {

        log.Fatal("CentOS does not support systemd user mode, so you will " +
        "not be able to run a non-root install with systemd managed services." +
        "Please set the serviceManagementMode variable in yba-installer-input.yml " +
        "to be equal to \"crontab\" instead.")

    }

    currentUser, _ := ExecuteBashCommand("bash", []string{"-c", "whoami"})
    currentUser = strings.ReplaceAll(strings.TrimSuffix(currentUser, "\n"), " ", "")

    command := "bash"
    args := []string{"-c", "ls /var/lib/systemd/linger"}
    output, _ := ExecuteBashCommand(command, args)

    outputTrimmed := strings.Replace(output, "\r\n", "", -1)

    if ! strings.Contains(outputTrimmed, currentUser) {

        log.Fatal("enable-linger has not been executed for the current user using " +
        "loginctl, so we you will not be able run a non-root install with systemd " +
        "managed services. Please run the command \"sudo loginctl enable-linger " +
        currentUser + " before continuing, or set the serviceManagementMode variable " +
        "in yba-installer-input.yml to be equal to \"crontab\" instead.")

    }

}

// PreflightRoot checks to see if the prerequisites for a non-root install of Yugabyte
// Anywhere are met.
func PreflightRoot() {

    checkPythonInstalled()
    checkPython3PipInstalled()
    checkJavaInstalled()

    // Only required in a non-root install that is systemd managed.
    if serviceManagementMode == "systemd" {

        checkUserLevelSystemdCapable()

    }

}
