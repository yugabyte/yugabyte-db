/*
 * Copyright (c) YugaByte, Inc.
 */

 package main

 import (
     "fmt"
     "os"
     "strings"
 )

 // Component 4: Nginx
 type Nginx struct {
     Name               string
     SystemdFileLocation string
     ConfFileLocation   string
     Mode               string
     ServerName         string

 }

 // SetUpPrereqs performs the setup operations specific
 // to Nginx.
 func (ngi Nginx) SetUpPrereqs() {
     arg1 := []string{"epel-release"}
     YumInstall(arg1)
     arg2 := []string{"nginx"}
     YumInstall(arg2)
 }

 // Install performs the installation procedures specific
 // to Nginx.
 func (ngi Nginx) Install() {
     if ngi.Mode == "https" {
         configureNginxConfHTTPS()
     }
 }

 func disableSELinux() {
    command0 := "sudo"
    arg0 := []string{"setenforce", "0"}
    ExecuteBashCommand(command0, arg0)
 }

 // Start performs the startup operations specific to Nginx.
 func (ngi Nginx) Start() {
     command1 := "systemctl"
     arg1 := []string{"daemon-reload"}
     ExecuteBashCommand(command1, arg1)

     if strings.Contains(DetectOS(), "CentOS") {
        disableSELinux()
    }

     command2 := "systemctl"
     arg2 := []string{"enable", "nginx"}
     ExecuteBashCommand(command2, arg2)

     command3 := "systemctl"
     arg3 := []string{"start", "nginx"}
     ExecuteBashCommand(command3, arg3)

     command4 := "systemctl"
     arg4 := []string{"status", "nginx"}
     ExecuteBashCommand(command4, arg4)
 }

 // Stop performs the stop operations specific to Nginx.
 func (ngi Nginx) Stop() {

     command1 := "systemctl"
     arg1 := []string{"stop", "nginx"}
     ExecuteBashCommand(command1, arg1)
 }

 // Restart performs the restart operations specific to Nginx.
 func (ngi Nginx) Restart() {

    if strings.Contains(DetectOS(), "CentOS") {
        disableSELinux()
    }
     command1 := "systemctl"
     arg1 := []string{"restart", "nginx"}
     ExecuteBashCommand(command1, arg1)
 }

 // GetConfFileLocation gets the location of the Nginx config
 // file.
 func (ngi Nginx) GetConfFileLocation() string {
     return ngi.ConfFileLocation
 }

 // Uninstall performs the uninstallation procedures specific
 // to Nginx (no data volumes to retain).
 func (ngi Nginx) Uninstall() {
     ngi.Stop()
 }

 func configureNginxConfHTTPS() {

    generateCertGolang()

    os.Chmod("key.pem", os.ModePerm)
    os.Chmod("cert.pem", os.ModePerm)

    os.MkdirAll("/opt/yugabyte/certs", os.ModePerm)
    fmt.Println("/opt/yugabyte/certs directory successfully created.")

    ExecuteBashCommand("bash", []string{"-c", "cp " + "key.pem" + " " + "/opt/yugabyte/certs"})
    ExecuteBashCommand("bash", []string{"-c", "cp " + "cert.pem" + " " + "/opt/yugabyte/certs"})

    command1 := "chown"
    arg1 := []string{"yugabyte:yugabyte", "/opt/yugabyte/certs/key.pem"}
    ExecuteBashCommand(command1, arg1)

    command2 := "chown"
    arg2 := []string{"yugabyte:yugabyte", "/opt/yugabyte/certs/cert.pem"}
    ExecuteBashCommand(command2, arg2)

    if strings.Contains(DetectOS(), "CentOS") {
        disableSELinux()
    }
}
