/*
 * Copyright (c) YugaByte, Inc.
 */

 package main

 import (
    "bytes"
    "fmt"
    "io/ioutil"
    "os"
    "strings"
 )

 // Component 1: Postgres
 type Postgres struct {
    Name                string
    SystemdFileLocation string
    ConfFileLocation    []string
    Version             string
 }

 // Method of the Component
 // Interface are implemented by
 // the Postgres struct and customizable
 // for each specific service.

 func (pg Postgres) SetUpPrereqs() {
    installPostgresRpmPackage()
    installPostgresLibraries()
 }

 func (pg Postgres) Install() {
    runInitDB()
    pg.Start()
    createYugawareDatabase()
    pg.editPgHbaConfFile(pg.ConfFileLocation[0])
 }

 func (pg Postgres) Start() {
    command1 := "systemctl"
    arg1 := []string{"enable", "postgresql-11.service"}
    ExecuteBashCommand(command1, arg1)

    command2 := "systemctl"
    arg2 := []string{"restart", "postgresql-11.service"}
    ExecuteBashCommand(command2, arg2)

    command3 := "systemctl"
    arg3 := []string{"status", "postgresql-11.service"}
    ExecuteBashCommand(command3, arg3)
 }

 func (pg Postgres) Stop() {
    command1 := "systemctl"
    arg1 := []string{"stop", "postgresql-11.service"}
    ExecuteBashCommand(command1, arg1)
 }

 func (pg Postgres) Restart() {
    command1 := "systemctl"
    arg1 := []string{"restart", "postgresql-11.service"}
    ExecuteBashCommand(command1, arg1)
 }

 func (pg Postgres) GetSystemdFile() string {
    return pg.SystemdFileLocation
 }

 func (pg Postgres) GetConfFile() []string {
    return pg.ConfFileLocation
 }

 // Per current cleanup.sh script.
 func (pg Postgres) Uninstall() {
    command1 := "dropdb"
    arg1 := []string{"-U", "postgres", "yugaware"}
    ExecuteBashCommand(command1, arg1)

    os.RemoveAll("/opt/yugabyte")
    }

 func (pg Postgres) VersionInfo() string {
    return pg.Version
 }

 func installPostgresRpmPackage() {

    command1 := "bash"
    arg1 := []string{"-c", "rpm -qa | grep pgdg-redhat"}
    output, _ := ExecuteBashCommand(command1, arg1)
    installed_string := strings.TrimSuffix(string(output), "\n")
    pg := "https://download.postgresql.org"
    full_name := pg + "/pub/repos/yum/reporpms/EL-7-x86_64/pgdg-redhat-repo-latest.noarch.rpm"
    arg_name := []string{"--disableplugin=subscription-manager", full_name}

    //If the Redhat RPM is already installed, then we don't have to install it again (takes time
    //for postgres to setup, optimize install).
    if installed_string != "" {
        file_name := strings.TrimSuffix(strings.ReplaceAll(installed_string, " ", ""), "\n")
        fmt.Println(file_name + " is already installed on the system, skipping.")
    } else {
        YumInstall(arg_name)
    }

 }

 func installPostgresLibraries() {

    arg1 := []string{"postgresql11", "postgresql11-server",
        "postgresql11-contrib", "postgresql11-libs"}
    YumInstall(arg1)

 }

 func runInitDB() {

    command1 := "/usr/pgsql-11/bin/postgresql-11-setup"
    arg1 := []string{"initdb"}
    os.RemoveAll("/var/lib/pgsql")
    ExecuteBashCommand(command1, arg1)

 }

 func createYugawareDatabase() {

    command1 := "dropdb"
    arg1 := []string{"-U", "postgres", "yugaware"}
    _, err1 := ExecuteBashCommand(command1, arg1)

    if err1 != nil {
        fmt.Println("Yugaware database doesn't exist yet, skipping drop!")
    } else {
        fmt.Println(command1 + " " + strings.Join(arg1, " ") + " executed succesfully!")
    }

    command2 := "su"
    arg2 := []string{"postgres", "-c", "createdb yugaware"}
    ExecuteBashCommand(command2, arg2)

 }

 func (pg Postgres) editPgHbaConfFile(pgHbaConfLocation string) {

    input1, err1 := ioutil.ReadFile(pgHbaConfLocation)
    if err1 != nil {
        fmt.Println(err1)
    }

    output1 := bytes.Replace(input1, []byte("peer"), []byte("trust"), -1)
    if err1 = ioutil.WriteFile(pgHbaConfLocation, output1, 0666); err1 != nil {
        fmt.Println(err1)
    }

    input2, err2 := ioutil.ReadFile(pgHbaConfLocation)
    if err2 != nil {
        fmt.Println(err2)
    }

    output2 := bytes.Replace(input2, []byte("ident"), []byte("trust"), -1)
    if err2 = ioutil.WriteFile(pgHbaConfLocation, output2, 0666); err2 != nil {
        fmt.Println(err2)
    }
 }
