/*
 * Copyright (c) YugaByte, Inc.
 */

 package main

 import (
     "fmt"
     "os"
 )

 // Component 4: Nginx
 type Nginx struct {
     Name               string
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
     certTLSstorage()
 }

 // Start performs the startup operations specific to Nginx.
 func (ngi Nginx) Start() {
     command1 := "systemctl"
     arg1 := []string{"daemon-reload"}
     ExecuteBashCommand(command1, arg1)

     command2 := "systemctl"
     arg2 := []string{"start", "nginx"}
     ExecuteBashCommand(command2, arg2)

     command3 := "systemctl"
     arg3 := []string{"status", "nginx"}
     ExecuteBashCommand(command3, arg3)
 }

 // Stop performs the stop operations specific to Nginx.
 func (ngi Nginx) Stop() {

     command1 := "systemctl"
     arg1 := []string{"stop", "nginx"}
     ExecuteBashCommand(command1, arg1)
 }

 // Restart performs the restart operations specific to Nginx.
 func (ngi Nginx) Restart() {

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

    os.MkdirAll("/opt/yugabyte/certs", os.ModePerm)
    fmt.Println("/opt/yugabyte/certs directory successfully created.")
    MoveFileGolang("key.pem", "/opt/yugabyte/certs/key.pem")
    MoveFileGolang("cert.pem", "/opt/yugabyte/certs/cert.pem")

    command1 := "chown"
    arg1 := []string{"yugabyte:yugabyte", "/opt/yugabyte/certs/key.pem"}
    ExecuteBashCommand(command1, arg1)

    command2 := "chown"
    arg2 := []string{"yugabyte:yugabyte", "/opt/yugabyte/certs/cert.pem"}
    ExecuteBashCommand(command2, arg2)
}

 func certTLSstorage() {

     os.MkdirAll("/opt/yugaware", os.ModePerm)
     fmt.Println("/opt/yugaware directory successfully created.")
     command1 := "chown"
     arg1 := []string{"yugabyte:yugabyte", "-R", "/opt/yugaware"}
     ExecuteBashCommand(command1, arg1)

 }
