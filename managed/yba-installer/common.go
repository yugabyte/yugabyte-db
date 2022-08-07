/*
 * Copyright (c) YugaByte, Inc.
 */

package main

import (
   "errors"
   "fmt"
   "github.com/hashicorp/go-version"
   "os"
   "strings"
)

// Component 5: Common (general setup operations)
type Common struct {
   Name    string
   Version string
   Mode    string
}

// Method of the Component
// Interface are implemented by
// the Common struct and customizable
// for each specific service.

func (com Common) SetUpPrereqs() {
   Version()
   License()
   Preflight("yba-installer-input-preflight.yml")
}

func (com Common) Install() {
   installPrerequisites()
   createYugabyteUser()
   GenerateTemplatedConfiguration(com.Version, com.Mode)
   downloadPlatformSupportPackageAndYugabundle(com.Version)
   copyThirdPartyDependencies()
}

func (com Common) Uninstall() {
   service0 := "yb-platform"
   service1 := "prometheus"
   service2 := "nginx"
   services := []string{service0, service1, service2}
   command := "service"

   for index, _ := range services {
      commandCheck0 := "bash"
      subCheck0 := "systemctl list-unit-files --type service | grep -w " + services[index]
      argCheck0 := []string{"-c", subCheck0}
      out0, _ := ExecuteBashCommand(commandCheck0, argCheck0)
      if strings.TrimSuffix(string(out0), "\n") != "" {
         argStop := []string{services[index], "stop"}
         ExecuteBashCommand(command, argStop)
      }
   }

}

func (com Common) Upgrade() {
    GenerateTemplatedConfiguration(com.Version, com.Mode)
    downloadPlatformSupportPackageAndYugabundle(com.Version)
    copyThirdPartyDependencies()
}

func installPrerequisites() {
   YumInstall([]string{"python3"})
   YumInstall([]string{"--enablerepo=extras", "epel-release"})
   YumInstall([]string{"python3-pip"})
   YumInstall([]string{"java-1.8.0-openjdk", "java-1.8.0-openjdk-devel"})
   FirewallCmdEnable([]string{"--add-port=22/tcp"})
   FirewallCmdEnable([]string{"--add-port=80/tcp"})
   FirewallCmdEnable([]string{"--add-port=5433/tcp"})
   FirewallCmdEnable([]string{"--add-port=9042/tcp"})
   FirewallCmdEnable([]string{"--add-port=6379/tcp"})
   FirewallCmdEnable([]string{"--add-port=7000/tcp"})
   FirewallCmdEnable([]string{"--add-port=7100/tcp"})
   FirewallCmdEnable([]string{"--add-port=9000/tcp"})
   FirewallCmdEnable([]string{"--add-port=9100/tcp"})
   FirewallCmdEnable([]string{"--add-port=9090/tcp"})
   ExecuteBashCommand("firewall-cmd", []string{"--reload"})
   commandPip := "pip3"
   argPip := []string{"install", "cryptography==3.3.2"}

   _, err:= ExecuteBashCommand(commandPip, argPip)

   if err != nil {

      YumInstall([]string{"redhat-rpm-config", "gcc", "libffi-devel",
      "python3-devel", "openssl-devel"})
      ExecuteBashCommand(commandPip, argPip)

   }

}

func createYugabyteUser() {
   command1 := "bash"
   arg1 := []string{"-c", "id -u yugabyte"}
   _, err := ExecuteBashCommand(command1, arg1)

   if err != nil {
      command2 := "useradd"
      arg2 := []string{"yugabyte"}
      ExecuteBashCommand(command2, arg2)
   } else {
      fmt.Println("User yugabyte already exists, skipping user creation.")
   }

   os.RemoveAll("/opt/yugabyte")
   os.MkdirAll("/opt/yugabyte", os.ModePerm)
   fmt.Println("/opt/yugabyte directory successfully created.")
   command3 := "chown"
   arg3 := []string{"yugabyte:yugabyte", "-R", "/opt/yugabyte"}
   ExecuteBashCommand(command3, arg3)
}

func downloadPlatformSupportPackageAndYugabundle(vers string) {

   command0 := "su"
   arg0 := []string{"yugabyte"}
   ExecuteBashCommand(command0, arg0)
   os.Chdir("/opt/yugabyte")
   os.RemoveAll("packages")

   YumInstall([]string{"wget"})
   command1 := "wget"
   v1, _ := version.NewVersion(vers)
   v2, _ := version.NewVersion("2.8.0.0")
   arg1 := []string{}
   path1 := ""

   if v1.LessThan(v2) {
      arg1 = []string{"https://downloads.yugabyte.com/platform-support-packages-2.6.tar.gz"}
      path1 = "/opt/yugabyte/platform-support-packages-2.6.tar.gz"
   } else {
      arg1 = []string{"https://downloads.yugabyte.com/platform-support-packages-2.12.tar.gz"}
      path1 = "/opt/yugabyte/platform-support-packages-2.12.tar.gz"
   }

   if _, err := os.Stat(path1); err == nil {
      fmt.Println("Package already exists at /opt/yugabyte, skipping download.")
   } else if errors.Is(err, os.ErrNotExist) {
      ExecuteBashCommand(command1, arg1)
   }

   rExtract1, errExtract1 := os.Open(path1)
   if errExtract1 != nil {
      fmt.Println("Error in starting the File Extraction process")
   }

   ExtractTarGz(rExtract1)
   fmt.Println(path1 + " successfully extracted!")

   errMove2 := os.Chdir("/opt/yugabyte/packages")
   if errMove2 != nil {
      panic(errMove2)
   }

   command2 := "wget"
   location := strings.Split(vers, "-")[0]
   d := "https://downloads.yugabyte.com"
   str2 := d + "/releases/" + location + "/yugabundle-" + vers + "-linux-x86_64.tar.gz"
   arg2 := []string{str2}
   path2 := "/opt/yugabyte/packages/yugabundle-" + vers + "-linux-x86_64.tar.gz"

   if _, err := os.Stat(path2); err == nil {
      fmt.Println("Package already exists at /opt/yugabyte/packages, skipping download.")
   } else if errors.Is(err, os.ErrNotExist) {
      if _, err := os.Stat("/opt/yugabyte/packages/yugabundle-" + vers + ".tar.gz"); err == nil {
         fmt.Println("Package already renamed at /opt/yugabyte/packages, skipping download.")
      } else {
         ExecuteBashCommand(command2, arg2)
      }
   }

   command3 := "mv"
   arg3 := []string{"yugabundle-" + vers + "-linux-x86_64.tar.gz",
      "yugabundle-" + vers + ".tar.gz"}
   path3 := "/opt/yugabyte/packages/yugabundle-" + vers + "-linux-x86_64.tar.gz"

   if _, err := os.Stat(path3); err == nil {
      ExecuteBashCommand(command3, arg3)
   } else if errors.Is(err, os.ErrNotExist) {
      if _, err := os.Stat("/opt/yugabyte/packages/yugabundle-" + vers + ".tar.gz"); err == nil {
         fmt.Println("Package has already been renamed at /opt/yugabyte/packages.")
      } else {
         fmt.Println("Package not present, please make sure it has been downloaded.")
      }
   }

   rExtract2, errExtract2 := os.Open("/opt/yugabyte/packages/yugabundle-" + vers + ".tar.gz")
   if errExtract2 != nil {
      fmt.Println("Error in starting the File Extraction process")
   }
   ExtractTarGzYugabundle(rExtract2)
   fmt.Println("/opt/yugabyte/packages/yugabundle-" + vers + ".tar.gz successfully extracted!")

}

func copyThirdPartyDependencies() {

   os.Chdir("/opt/yugabyte/")
   os.RemoveAll("third-party")
   rExtract, errExtract := os.Open("/opt/yugabyte/packages/thirdparty-deps.tar.gz")
   if errExtract != nil {
      fmt.Println("Error in starting the File Extraction process")
   }
   if _, err := os.Stat("/opt/yugabyte/prometheus-dependencies/"); err == nil {
      fmt.Println("/opt/yugabyte/prometheus-dependencies/ exists, skipping re-extract.")
   } else {
      ExtractTarGz(rExtract)
      fmt.Println("/opt/yugabyte/packages/thirdparty-deps.tar.gz successfully extracted!")
   }
   MoveFileGolang("prometheus-dependencies", "third-party")
}
