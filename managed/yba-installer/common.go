/*
 * Copyright (c) YugaByte, Inc.
 */

 package main

 import (
    "fmt"
    "os"
    "strings"
    "strconv"
    "log"
    "github.com/spf13/viper"
 )

 // Common (general setup operations)
 type Common struct {
    Name    string
    Version string
    Mode    string
 }

 // SetUpPrereqs performs the setup operations common to
 // all services.
 func (com Common) SetUpPrereqs() {
    fmt.Println("You are on version " + versionToInstall +
    " of Yba-installer!")
    License()
    Preflight("yba-installer-input.yml")
 }

 // Install performs the installation procedures common to
 // all services.
 func (com Common) Install() {
    installPrerequisites()
    createYugabyteUser()
    GenerateTemplatedConfiguration()
    com.extractPlatformSupportPackageAndYugabundle(com.Version)
    com.renameThirdPartyDependencies()
 }

 // Uninstall performs the uninstallation procedures common to
 // all services.
 func (com Common) Uninstall() {
    service0 := "yb-platform"
    service1 := "prometheus"
    service2 := "nginx"
    services := []string{service0, service1, service2}
    command := "service"

    for index := range services {
       commandCheck0 := "bash"
       subCheck0 := "systemctl list-unit-files --type service | grep -w " + services[index]
       argCheck0 := []string{"-c", subCheck0}
       out0, _ := ExecuteBashCommand(commandCheck0, argCheck0)
       if strings.TrimSuffix(string(out0), "\n") != "" {
          argStop := []string{services[index], "stop"}
          ExecuteBashCommand(command, argStop)
       }
    }
    RemoveAllExceptDataVolumes([]string{"platform", "prometheus"})
 }

 // RemoveAllExceptDataVolumes removes the install directory from the host's
 // operating system, except for data volumes specified in the input config
 // file.
 func RemoveAllExceptDataVolumes(services []string) {

      viper.SetConfigFile("yba-installer-input.yml")
      viper.ReadInConfig()
      baseDirs := fmt.Sprint(viper.Get("basedirs"))
      baseDirs = baseDirs[1:len(baseDirs) - 1]
      splitBaseDirs := strings.Split(baseDirs, " ")
      for _, baseDir := range(splitBaseDirs) {
      // Only remove all except data volumes if the base directories exist.
      if _, err := os.Stat(baseDir); err == nil {
         splitBaseDir := strings.Split(baseDir, "/")
         baseDirOneUp := strings.Join(splitBaseDir[0:len(splitBaseDir)  - 1], "/")
         for _, service := range(services) {
          dataVolumes := fmt.Sprint(viper.Get("datavolumes." + service))
          dataVolumes = dataVolumes[1:len(dataVolumes) - 1]
          splitDataVolumes := strings.Split(dataVolumes, " ")
          for _, volume := range(splitDataVolumes) {
           // Only move out the data volume if it exists.
            if _, err := os.Stat(volume); err == nil {
             if strings.Contains(volume, baseDir) {
                 volumeMoved := strings.ReplaceAll(volume, baseDir, baseDirOneUp)
                 MoveFileGolang(volume, volumeMoved)
               }
             }
           }
         }
          os.RemoveAll(baseDir)
          os.MkdirAll(baseDir, os.ModePerm)
          for _, service := range(services) {
            dataVolumes := fmt.Sprint(viper.Get("datavolumes." + service))
            dataVolumes = dataVolumes[1:len(dataVolumes) - 1]
            splitDataVolumes := strings.Split(dataVolumes, " ")
            for _, volume := range(splitDataVolumes) {
             if strings.Contains(volume, baseDir) {
                 volumeMoved := strings.ReplaceAll(volume, baseDir, baseDirOneUp)
                  // Only move in the data volume if it exists.
                 if _, err := os.Stat(volumeMoved); err == nil {
                     MoveFileGolang(volumeMoved, volume)
                 }
                 }
               }
             }
           }
        }
     }

 // Upgrade performs the upgrade procedures common to all services.
 func (com Common) Upgrade() {
     GenerateTemplatedConfiguration()
     com.extractPlatformSupportPackageAndYugabundle(com.Version)
     com.renameThirdPartyDependencies()
 }

 func installPrerequisites() {
    var bringOwnPython, errPython = strconv.ParseBool(getYamlPathData(".python.bringOwn"))

    if errPython != nil {
        log.Fatal("Please set python.BringOwn to either true or false!")
    }

    if !bringOwnPython {
       YumInstall([]string{"python3"})
    }
    YumInstall([]string{"--enablerepo=extras", "epel-release"})
    if !bringOwnPython {
       YumInstall([]string{"python3-pip"})
    }
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

   os.MkdirAll("/opt/yugabyte", os.ModePerm)
   fmt.Println("/opt/yugabyte directory successfully created.")
   command3 := "chown"
   arg3 := []string{"yugabyte:yugabyte", "-R", "/opt/yugabyte"}
   ExecuteBashCommand(command3, arg3)
}

func (com Common) extractPlatformSupportPackageAndYugabundle(vers string) {

   command0 := "su"
   arg0 := []string{"yugabyte"}
   ExecuteBashCommand(command0, arg0)
   os.RemoveAll("/opt/yugabyte/packages")

   path0 := "yugabundle-"+vers+"-centos-x86_64.tar.gz"

   rExtract1, errExtract1 := os.Open(path0)
   if errExtract1 != nil {
      fmt.Println("Error in starting the File Extraction process")
   }

   Untar(rExtract1, "/opt/yugabyte")

   path1 := "/opt/yugabyte/yugabyte-"+vers+
   "/yugabundle_support-"+vers+"-centos-x86_64.tar.gz"

   rExtract2, errExtract2 := os.Open(path1)
   if errExtract2 != nil {
      fmt.Println("Error in starting the File Extraction process")
   }

   Untar(rExtract2, "/opt/yugabyte")

   fmt.Println(path1 + " successfully extracted!")

   MoveFileGolang("/opt/yugabyte/yugabyte-"+vers,
   "/opt/yugabyte/packages/yugabyte-"+vers)

}

func (com Common) renameThirdPartyDependencies() {

   //Remove any thirdparty directories if they already exist, so
   //that the install action is idempotent.
   os.RemoveAll("/opt/yugabyte/thirdparty")
   rExtract, _ := os.Open("/opt/yugabyte/packages/thirdparty-deps.tar.gz")
   Untar(rExtract, "/opt/yugabyte")
   fmt.Println("/opt/yugabyte/packages/thirdparty-deps.tar.gz successfully extracted!")
   MoveFileGolang("/opt/yugabyte/thirdparty", "/opt/yugabyte/third-party")
}
