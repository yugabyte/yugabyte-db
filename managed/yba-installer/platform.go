/*
 * Copyright (c) YugaByte, Inc.
 */

package main

import (
   "fmt"
   "log"
   "os"
)

// Component 3: Platform
type Platform struct {
   Name                string
   SystemdFileLocation string
   ConfFileLocation    string
   Version             string
   CorsOrigin          string
   UseOIDCSso          bool
}

// Method of the Component
// Interface are implemented by
// the Platform struct and customizable
// for each specific service.

func (plat Platform) Install() {
   os.Chdir("/opt/yugabyte/packages")
   fileName := "install.sh"
   err := os.Chmod(fileName, 0777)

   if err != nil {
      log.Fatal(err)
   } else {
      fmt.Println("Install script has now been given executable permissions!")
   }

   fmt.Println("The version of Platform that you are about to install is " + plat.Version)
   command1 := "/bin/sh"
   arg1 := []string{fileName, plat.Version}
   ExecuteBashCommand(command1, arg1)

}

func (plat Platform) Start() {
 command1 := "systemctl"
 arg1 := []string{"daemon-reload"}
 ExecuteBashCommand(command1, arg1)

 command2 := "systemctl"
 arg2 := []string{"start", "yb-platform.service"}
 ExecuteBashCommand(command2, arg2)

 command3 := "systemctl"
 arg3 := []string{"status", "yb-platform.service"}
 ExecuteBashCommand(command3, arg3)
}

func (plat Platform) Stop() {
   command1 := "systemctl"
   arg1 := []string{"stop", "yb-platform.service"}
   ExecuteBashCommand(command1, arg1)
}

func (plat Platform) Restart() {
   command1 := "systemctl"
   arg1 := []string{"restart", "yb-platform.service"}
   ExecuteBashCommand(command1, arg1)
}

func (plat Platform) GetSystemdFile() string {
   return plat.SystemdFileLocation
}

func (plat Platform) GetConfFile() string {
   return plat.ConfFileLocation
}

// Per current cleanup.sh script.
func (plat Platform) Uninstall() {
   plat.Stop()
   os.RemoveAll("/opt/yugabyte")
}

func (plat Platform) VersionInfo() string {
   return plat.Version
}
