/*
 * Copyright (c) YugaByte, Inc.
 */

 package main

 import (
      "errors"
      "fmt"
      "os"
 )

 // Component 2: Prometheus
 type Prometheus struct {
    Name                string
    SystemdFileLocation string
    ConfFileLocation    string
    Version             string
    IsUpgrade           bool
 }

 // Method of the Component
 // Interface are implemented by
 // the Prometheus struct and customizable
 // for each specific service.

 func (prom Prometheus) SetUpPrereqs() {
    prom.moveAndExtractPrometheusPackage(prom.Version)
 }

 func (prom Prometheus) Install() {
    createPrometheusUser(prom.IsUpgrade)
    prom.createPrometheusSymlinks(prom.Version, prom.IsUpgrade)
    setPermissionsPrometheusConfigFile(prom.IsUpgrade)
    restartIfUpgrade(prom.IsUpgrade)
 }

 func (prom Prometheus) Start() {
    command1 := "systemctl"
    arg1 := []string{"daemon-reload"}
    ExecuteBashCommand(command1, arg1)

    command2 := "systemctl"
    arg2 := []string{"enable", "prometheus"}
    ExecuteBashCommand(command2, arg2)

    command3 := "systemctl"
    arg3 := []string{"start", "prometheus"}
    ExecuteBashCommand(command3, arg3)

    command4 := "systemctl"
    arg4 := []string{"status", "prometheus"}
    ExecuteBashCommand(command4, arg4)
 }

 func (prom Prometheus) Stop() {
    command1 := "systemctl"
    arg1 := []string{"stop", "prometheus"}
    ExecuteBashCommand(command1, arg1)
 }

 func (prom Prometheus) Restart() {
    command1 := "systemctl"
    arg1 := []string{"restart", "prometheus"}
    ExecuteBashCommand(command1, arg1)
 }

 func (prom Prometheus) GetSystemdFile() string {
    return prom.SystemdFileLocation
 }

 func (prom Prometheus) GetConfFile() string {
    return prom.ConfFileLocation
 }

 //Per current cleanup.sh script.
 func (prom Prometheus) Uninstall() {
    prom.Stop()
    RemoveAllExceptDataVolumes([]string{"prometheus"})
 }

 func (prom Prometheus) VersionInfo() string {
    return prom.Version
 }

 func (prom Prometheus) moveAndExtractPrometheusPackage(ver string) {

    srcPath := "/opt/yugabyte/third-party/prometheus-" + ver + ".linux-amd64.tar.gz"
    dstPath := "/opt/yugabyte/packages/prometheus-" + ver + ".linux-amd64.tar.gz"
    CopyFileGolang(srcPath, dstPath)
    rExtract, errExtract := os.Open(dstPath)
    if errExtract != nil {
        fmt.Println("Error in starting the File Extraction process")
    }

    path_package_extracted := "/opt/yugabyte/packages/prometheus-" + ver + ".linux-amd64"
    if _, err := os.Stat(path_package_extracted); err == nil {
        fmt.Println(path_package_extracted + " already exists, skipping re-extract.")
    } else {
        Untar(rExtract, "/opt/yugabyte/packages")
        fmt.Println(dstPath + " successfully extracted!")
    }

 }

 func createPrometheusUser(isUpgrade bool) {

    if !isUpgrade {

        command1 := "bash"
        args1 := []string{"-c", "id -u prometheus"}
        _, err := ExecuteBashCommand(command1, args1)

        if err != nil {

            command2 := "useradd"
            arg2 := []string{"--no-create-home", "--shell", "/bin/false", "prometheus"}
            ExecuteBashCommand(command2, arg2)

        } else {
            fmt.Println("User prometheus already exists, skipping user creation.")
        }

        os.MkdirAll("/etc/prometheus", os.ModePerm)
        fmt.Println("/etc/prometheus directory successfully created.")

        os.MkdirAll("/var/lib/prometheus", os.ModePerm)
        fmt.Println("/var/lib/prometheus directory successfully created.")

        //Make the swamper_targets directory for prometheus
        os.MkdirAll("/opt/yugabyte/swamper_targets", os.ModePerm)

        command3 := "chown"
        arg3 := []string{"prometheus:prometheus", "/etc/prometheus"}
        ExecuteBashCommand(command3, arg3)

        command4 := "chown"
        arg4 := []string{"prometheus:prometheus", "/var/lib/prometheus"}
        ExecuteBashCommand(command4, arg4)

    }

 }

 func (prom Prometheus) createPrometheusSymlinks(ver string, isUpgrade bool) {

    command1 := "ln"
    path1a := "/opt/yugabyte/packages/prometheus-" + ver + ".linux-amd64/prometheus"
    path1b := "/usr/local/bin/prometheus"
    arg1 := []string{"-sf", path1a, path1b}

    if _, err := os.Stat("/usr/local/bin/prometheus"); err == nil {
        os.Remove("/usr/local/bin/prometheus")
        ExecuteBashCommand(command1, arg1)
    } else if errors.Is(err, os.ErrNotExist) {
        ExecuteBashCommand(command1, arg1)
    }

    os.Chmod("/usr/local/bin/prometheus", os.ModePerm)

    if !isUpgrade {
        command2 := "chown"
        arg2 := []string{"prometheus:prometheus", "/usr/local/bin/prometheus"}
        ExecuteBashCommand(command2, arg2)
    }

    command3 := "ln"
    path3a := "/opt/yugabyte/packages/prometheus-" + ver + ".linux-amd64/promtool"
    path3b := "/usr/local/bin/promtool"
    arg3 := []string{"-sf", path3a, path3b}

    if _, err := os.Stat("/usr/local/bin/promtool"); err == nil {
        os.Remove("/usr/local/bin/promtool")
        ExecuteBashCommand(command3, arg3)
    } else if errors.Is(err, os.ErrNotExist) {
        ExecuteBashCommand(command3, arg3)
    }

    if !isUpgrade {
        command4 := "chown"
        arg4 := []string{"prometheus:prometheus", "/usr/local/bin/promtool"}
        ExecuteBashCommand(command4, arg4)
    }

    command5 := "ln"
    path5a := "/opt/yugabyte/packages/prometheus-" + ver + ".linux-amd64/consoles/"
    path5b := "/etc/prometheus/"
    arg5 := []string{"-sf", path5a, path5b}

    if _, err := os.Stat("/etc/prometheus/consoles"); err == nil {
        os.Remove("/etc/prometheus/consoles")
        ExecuteBashCommand(command5, arg5)
    } else if errors.Is(err, os.ErrNotExist) {
        ExecuteBashCommand(command5, arg5)
    }

    if !isUpgrade {
        command6 := "chown"
        arg6 := []string{"-R", "prometheus:prometheus", "/etc/prometheus/consoles"}
        ExecuteBashCommand(command6, arg6)
    }

    command7 := "ln"
    path7a := "/opt/yugabyte/packages/prometheus-" + ver + ".linux-amd64/console_libraries/"
    path7b := "/etc/prometheus/"
    arg7 := []string{"-sf", path7a, path7b}

    if _, err := os.Stat("/etc/prometheus/console_libraries"); err == nil {
        os.Remove("/etc/prometheus/console_libraries")
        ExecuteBashCommand(command7, arg7)

    } else if errors.Is(err, os.ErrNotExist) {
        ExecuteBashCommand(command7, arg7)
    }

    if !isUpgrade {
        command8 := "chown"
        arg8 := []string{"-R", "prometheus:prometheus", "/etc/prometheus/console_libraries"}
        ExecuteBashCommand(command8, arg8)
    }
}

 func setPermissionsPrometheusConfigFile(isUpgrade bool) {

    if !isUpgrade {
        command1 := "chown"
        arg1 := []string{"-R", "prometheus:prometheus", "/etc/prometheus/prometheus.yml"}
        ExecuteBashCommand(command1, arg1)
    }
 }

 func restartIfUpgrade(isUpgrade bool) {

    if isUpgrade {
        command1 := "systemctl"
        arg1 := []string{"daemon-reload"}
        ExecuteBashCommand(command1, arg1)

        command2 := "systemctl"
        arg2 := []string{"restart", "prometheus"}
        ExecuteBashCommand(command2, arg2)

        command3 := "systemctl"
        arg3 := []string{"status", "prometheus"}
        ExecuteBashCommand(command3, arg3)
    }

 }
