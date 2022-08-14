    /*
    * Copyright (c) YugaByte, Inc.
    */

    package main

    import (
    "log"
    "strings"
    "os"
    "errors"
    )

    // // Actions that require root for us to execute are not possible under a non-root
    // // installation. We will simply add them to the preflight checks so that the user
    // // will know to perform these necessary installs prior to running Yba-installer
    // // themselves.

    func checkPythonInstalled() {

    command := "bash"
    args := []string{"-c", "command -v python3"}
    output, _ := ExecuteBashCommand(command, args)

    outputTrimmed := strings.Replace(output, "\r\n", "", -1)

    if outputTrimmed == "" {

    log.Fatal("Python 3 not installed on the system ," +
    "please install Python 3 before continuing. As an example, " +
    "CentOS users can invoke the command \"sudo yum install -y " +
    "python3\"" + " in order to do so.")

    }

    }

    func checkEpelReleaseEnabled() {

    command := "bash"
    args := []string{"-c", "yum repolist"}
    output, _ := ExecuteBashCommand(command, args)

    outputTrimmed := strings.Replace(output, "\r\n", "", -1)

    if !strings.Contains(outputTrimmed, "epel") {

        log.Fatal("The EPEL repo has not been enabled, " +
        "please enable it before continuing.  As an example, " +
        "CentOS users can invoke the command \"sudo yum --enablerepo=extras " +
        "install epel-release\"" + " in order to do so.")
    }
    }


    func checkPython3PipInstalled() {

    command := "bash"
    args := []string{"-c", "command -v pip"}
    output, _ := ExecuteBashCommand(command, args)

    outputTrimmed := strings.Replace(output, "\r\n", "", -1)

    if outputTrimmed == "" {

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
        "to do so")
    }

    }

    func checkYugabyteUserExists() {

    command1 := "bash"
    arg1 := []string{"-c", "id -u yugabyte"}
    _, err := ExecuteBashCommand(command1, arg1)

    if err != nil {

    log.Fatal("Yugabyte User does not exist on the system, " +
    "please create this user before continuing. The Yugabyte User " +
    "can be created using the command \"sudo useradd yugabyte\".")

    }
    }

    func checkYugabyteDirectoryExists() {


    if _, err := os.Stat("/opt/yugabyte"); err != nil {

    log.Fatal("/opt/yugabyte does not exist, " +
    "please create this directory before continuing. This directory " +
    "can be created using the command \"sudo mkdir /opt/yugabyte\".")

    }

    }

    func checkYugabyteChownPermissions() {

    command := "bash"
    args := []string{"-c", "find /opt/yugabyte -user yugabyte"}
    output, _ := ExecuteBashCommand(command, args)

    outputTrimmed := strings.Replace(output, "\r\n", "", -1)

    if outputTrimmed == "" {

    log.Fatal("User yugabyte does not have ownership of " +
    " /opt/yugabyte, please provide this before continuing. Ownership can" +
    " be provided using the command \"sudo chown yugabyte:yugabyte -R " +
    "/opt/yugabyte\".")

    }

    }

    func checkPrometheusUserExists() {

    command1 := "bash"
    arg1 := []string{"-c", "id -u prometheus"}
    _, err := ExecuteBashCommand(command1, arg1)

    if err != nil {

    log.Fatal("Prometheus User does not exist on the system, " +
    "please create this user before continuing. This user can " +
    "be created using the command \"sudo useradd --no-create-home " +
    "--shell /bin/false prometheus\".")

    }

    }

    func checkPrometheusDirectoryExists() {


    if _, err := os.Stat("/etc/prometheus"); errors.Is(err, os.ErrNotExist) {

    log.Fatal("/etc/prometheus does not exist, " +
    "please create this directory before continuing. This directory " +
    "can be created using the command \"sudo mkdir /etc/prometheus\".")

    }

    }

    func checkPrometheusStoragePathExists() {


    if _, err := os.Stat("/var/lib/prometheus"); errors.Is(err, os.ErrNotExist) {

    log.Fatal("/var/lib/prometheus does not exist, " +
    "please create this directory before continuing. This directory " +
    "can be created using the command \"sudo mkdir /var/lib/prometheus\".")

    }

    }

    func checkPrometheusDirectoryChownPermissions() {

    command := "bash"
    args := []string{"-c", "find /etc/prometheus -user prometheus"}
    output, _ := ExecuteBashCommand(command, args)

    outputTrimmed := strings.Replace(output, "\r\n", "", -1)

    if outputTrimmed == "" {

    log.Fatal("User prometheus does not have ownership of " +
    "/etc/prometheus, please provide this before continuing." +
    "Ownership can be provided using the command " +
    "\"sudo chown prometheus:prometheus /etc/prometheus\".")

        }

    }

    func checkPrometheusStoragePathChownPermissions() {

    command := "bash"
    args := []string{"-c", "find /var/lib/prometheus -user prometheus"}
    output, _ := ExecuteBashCommand(command, args)

    outputTrimmed := strings.Replace(output, "\r\n", "", -1)

    if outputTrimmed == "" {

    log.Fatal("User prometheus does not have ownership of " +
    "/var/lib/prometheus, please provide this before continuing." +
    "Ownership can be provided using the command " +
    "\"sudo chown prometheus:prometheus /var/lib/prometheus\".")

        }

    }

    func checkPrometheusConsolesChownPermissions() {

        command := "bash"
        args := []string{"-c",
        "find /etc/prometheus/consoles -user prometheus"}
        output, _ := ExecuteBashCommand(command, args)

        outputTrimmed := strings.Replace(output, "\r\n", "", -1)

        if outputTrimmed == "" {

        log.Fatal("User prometheus does not have ownership of " +
        "/etc/prometheus/consoles, please provide this before continuing." +
        "Ownership can be provided using the command " +
        "\"sudo chown prometheus:prometheus /etc/prometheus/consoles\".")

            }

        }

    func checkPrometheusConsoleLibrariesChownPermissions() {

        command := "bash"
        args := []string{"-c",
         "find /etc/prometheus/console_libraries -user prometheus"}
        output, _ := ExecuteBashCommand(command, args)

        outputTrimmed := strings.Replace(output, "\r\n", "", -1)

        if outputTrimmed == "" {

        log.Fatal("User prometheus does not have ownership of " +
        "/etc/prometheus/consoles_libraries, please provide this before continuing." +
        "Ownership can be provided using the command " +
        "\"sudo chown prometheus:prometheus /etc/prometheus/console_libraries\".")

            }

        }

    func PreflightRoot() {

    checkPythonInstalled()
    checkEpelReleaseEnabled()
    checkPython3PipInstalled()
    checkJavaInstalled()
    checkYugabyteUserExists()
    checkYugabyteDirectoryExists()
    checkYugabyteChownPermissions()
    checkPrometheusUserExists()
    checkPrometheusDirectoryExists()
    checkPrometheusStoragePathExists()
    checkPrometheusDirectoryChownPermissions()
    checkPrometheusStoragePathChownPermissions()
    checkPrometheusConsolesChownPermissions()
    checkPrometheusConsoleLibrariesChownPermissions()
    }
