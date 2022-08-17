/*
 * Copyright (c) YugaByte, Inc.
 */

 package main

 import (
    "fmt"
    "log"
    "os"
    "io/ioutil"
    "strings"
    "errors"
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

    createNecessaryDirectories(plat.Version)
    extractPackageInsidePackageFolder(plat.Version)
    createDevopsAndYugawareDirectories(plat.Version)
    untarDevopsAndYugawarePackages(plat.Version)
    copyYugabyteReleaseFile(plat.Version)
    installDevopsEnvironment(plat.Version)
    renameAndCreateSymlinks(plat.Version)
 }

 func createNecessaryDirectories(version string) {

    installPath := "/opt/yugabyte"

    os.MkdirAll(installPath+"/releases/"+version, os.ModePerm)
    os.MkdirAll(installPath+"/swamper_targets", os.ModePerm)
    os.MkdirAll(installPath+"/data", os.ModePerm)
    os.MkdirAll(installPath+"/third-party", os.ModePerm)

 }

 func extractPackageInsidePackageFolder(version string) {

    packageName := "yugabundle-" + version + ".tar.gz"
    rExtract, errExtract := os.Open("/opt/yugabyte/packages/" + packageName)
    if errExtract != nil {
       log.Fatalf("Error in starting the File Extraction process")
    }
    Untar(rExtract, "/opt/yugabyte/packages")

 }

 func createDevopsAndYugawareDirectories(version string) {

    installPath := "/opt/yugabyte"
    packageFolder := "yugabyte-" + version
    os.MkdirAll(installPath+"/packages/"+packageFolder+"/devops", os.ModePerm)
    os.MkdirAll(installPath+"/packages/"+packageFolder+"/yugaware", os.ModePerm)

 }

 func untarDevopsAndYugawarePackages(version string) {

    installPath := "/opt/yugabyte"
    packageFolder := "yugabyte-" + version
    packageFolderPath := installPath+"/packages/"+packageFolder

    files, err := ioutil.ReadDir(packageFolderPath)
    if err != nil {
        log.Fatal(err)
    }

    for _, f := range files {
        if strings.Contains(f.Name(), "devops") {

          devopsTgzName := f.Name()
          devopsTgzPath := packageFolderPath + "/" + devopsTgzName
          rExtract, errExtract := os.Open(devopsTgzPath)
          if errExtract != nil {
             log.Fatalf("Error in starting the File Extraction process")
          }

          Untar(rExtract, packageFolderPath + "/devops")


        } else if strings.Contains(f.Name(), "yugaware") {

          yugawareTgzName := f.Name()
          yugawareTgzPath := packageFolderPath + "/" + yugawareTgzName
          rExtract, errExtract := os.Open(yugawareTgzPath)
          if errExtract != nil {
             log.Fatalf("Error in starting the File Extraction process")
          }

          Untar(rExtract, packageFolderPath + "/yugaware")

        }
    }

 }

 func copyYugabyteReleaseFile(version string) {

    installPath := "/opt/yugabyte"
    packageFolder := "yugabyte-" + version
    packageFolderPath := installPath+"/packages/"+packageFolder

    files, err := ioutil.ReadDir(packageFolderPath)
    if err != nil {
        log.Fatal(err)
    }

    for _, f := range files {
       if strings.Contains(f.Name(), "yugabyte") {

          yugabyteTgzName := f.Name()
          yugabyteTgzPath := packageFolderPath + "/" + yugabyteTgzName
          CopyFileGolang(yugabyteTgzPath,
             installPath + "/releases/" + version +"/" + yugabyteTgzName)

       }
    }
 }

 // Method will be reworked later when we replace the Devops Env with the PEX
 // generation.
 func installDevopsEnvironment(version string) {

    installPath := "/opt/yugabyte"
    packageFolder := "yugabyte-" + version
    packageFolderPath := installPath+"/packages/"+packageFolder
    fileName := "install_python_requirements.sh"

    filePath := packageFolderPath + "/devops/bin/" + fileName

    err := os.Chmod(filePath, 0777)

    if err != nil {
       log.Fatal(err)
    } else {
       fmt.Println("Devops Python script has now been given executable permissions!")
    }

    command1 := "/bin/sh"
    arg1 := []string{filePath, "--use_package"}
    ExecuteBashCommand(command1, arg1)

 }

 func renameAndCreateSymlinks(version string) {

    installPath := "/opt/yugabyte"
    packageFolder := "yugabyte-" + version
    packageFolderPath := installPath+"/packages/"+packageFolder

    command1 := "ln"
    path1a := packageFolderPath + "/yugaware"
    path1b := installPath + "/yugaware"
    arg1 := []string{"-sf", path1a, path1b}

    if _, err := os.Stat(path1b); err == nil {
       pathBackup := packageFolderPath + "/yugaware_backup"
       MoveFileGolang(path1b, pathBackup)
    } else if errors.Is(err, os.ErrNotExist) {
        ExecuteBashCommand(command1, arg1)
    }

    command2 := "ln"
    path2a := packageFolderPath + "/devops"
    path2b := installPath + "/devops"
    arg2 := []string{"-sf", path2a, path2b}

    if _, err := os.Stat(path2b); err == nil {
       pathBackup := packageFolderPath + "/devops_backup"
       MoveFileGolang(path2b, pathBackup)
    } else if errors.Is(err, os.ErrNotExist) {
        ExecuteBashCommand(command2, arg2)
    }

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
    RemoveAllExceptDataVolumes([]string{"platform"})
 }

 func (plat Platform) VersionInfo() string {
    return plat.Version
 }
