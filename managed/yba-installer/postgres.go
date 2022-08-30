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
    "log"
    "time"
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

 func (pg Postgres) SetUpPrereqsBundled() {

   // Only extract the Postgres package if it doesn't exist.
   if _, err := os.Stat("/var/lib/pgsql"); err != nil {
      extractPostgresPackageBundled()
   }

 }

 func (pg Postgres) InstallBundled() {

   runInitDBBundled()
   //Need to edit PostgresConf before creating Yugaware database.
   pg.editPostgresConf(pg.ConfFileLocation[1])
   //5 second sleep to give enough time for database to start up, so
   //that createYugawareDatabaseBundled() can execute succesfully (5 seconds
   //on each side)
   time.Sleep(5 * time.Second)
   pg.StartBundled()
   log.Println("About to create Yugaware database...")
   time.Sleep(5 * time.Second)
   createYugawareDatabaseBundled()

 }

 func (pg Postgres) RestartBundled() {

   pg.StopBundled()
   time.Sleep(5 * time.Second)
   pg.StartBundled()

 }

 func (pg Postgres) StartBundled() {
   log.Println("Starting the Postgres service...")
   command1 := "sudo"
   arg1 := []string{"-u", "postgres", "bash", "-c",
   "/var/lib/pgsql/bin/pg_ctl -D /var/lib/pgsql/data start " +
   "-l /var/lib/pgsql/logfile -m smart"}
   ExecuteBashCommand(command1, arg1)
   log.Println("Postgres service started succesfully!")

 }

 func (pg Postgres) StopBundled() {
   log.Println("Stopping the Postgres service...")
   command1 := "sudo"
   arg1 := []string{"-u", "postgres", "bash", "-c",
   "/var/lib/pgsql/bin/pg_ctl -D /var/lib/pgsql/data " +
   "-l /var/lib/pgsql/logfile stop"}
   ExecuteBashCommand(command1, arg1)
   log.Println("Postgres service stopped succesfully!")
 }

 func extractPostgresPackageBundled() {

   binaryName := "postgresql-9.6.24-1-linux-x64-binaries.tar.gz"

   command1 := "bash"
   arg1 := []string{"-c", "tar -zvxf " + binaryName + " -C /var/lib"}

   ExecuteBashCommand(command1, arg1)

 }

 func runInitDBBundled() {

   commandCheck := "bash"
   argsCheck := []string{"-c", "id -u postgres"}
   _, err := ExecuteBashCommand(commandCheck, argsCheck)

   if err != nil {

       commandUser := "useradd"
       argUser := []string{"--no-create-home", "--shell", "/bin/false", "postgres"}
       ExecuteBashCommand(commandUser, argUser)

   } else {
       fmt.Println("User postgres already exists, skipping user creation.")
   }

   // Need to give the postgres user ownership of the /var/lib/pgsql directory,
   // since we cannot execute initdb as root.
   command0 := "chown"
   arg0 := []string{"postgres", "/var/lib/pgsql"}

   ExecuteBashCommand(command0, arg0)

   // Create and provide logfile so that the terminal does not hang. Give the
   // Postgres user ownership of this file as well.
   os.Create("/var/lib/pgsql/logfile")

   command1 := "chown"
   arg1 := []string{"postgres", "/var/lib/pgsql/logfile"}

   ExecuteBashCommand(command1, arg1)

   // Create the mount directory for Postgres to execute on (not default /tmp)
   os.MkdirAll("/var/run/postgresql", os.ModePerm)

   command2 := "chown"
   arg2 := []string{"postgres", "/var/run/postgresql"}

   ExecuteBashCommand(command2, arg2)

   command3 := "sudo"
   arg3 := []string{"-u", "postgres", "bash", "-c",
   "/var/lib/pgsql/bin/initdb -U postgres -D /var/lib/pgsql/data"}
   ExecuteBashCommand(command3, arg3)

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
    //RemoveAllExceptDataVolumes([]string{"postgres"})
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

 func createYugawareDatabaseBundled() {

   mountPath := "/var/run/postgresql/"

   command1 := "sudo"
   arg1 := []string{"-u", "postgres", "bash", "-c", "/var/lib/pgsql/bin/dropdb -h " +
   mountPath + " yugaware"}
   _, err1 := ExecuteBashCommand(command1, arg1)

   if err1 != nil {
       fmt.Println("Yugaware database doesn't exist yet, skipping drop!")
   } else {
       fmt.Println(command1 + " " + strings.Join(arg1, " ") + " executed succesfully!")
   }

   command2 := "sudo"
   arg2 := []string{"-u", "postgres", "bash", "-c", "/var/lib/pgsql/bin/createdb -h " +
   mountPath + " yugaware"}
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

 func (pg Postgres) editPostgresConf(postgresConfLocation string) {

   input1, err1 := ioutil.ReadFile(postgresConfLocation)
   if err1 != nil {
       fmt.Println(err1)
   }

   stringToReplace := "#unix_socket_directories = '/tmp'"
   stringToReplaceWith := "unix_socket_directories = '/var/run/postgresql/'"

   output1 := bytes.Replace(input1, []byte(stringToReplace), []byte(stringToReplaceWith), -1)
   if err1 = ioutil.WriteFile(postgresConfLocation, output1, 0666); err1 != nil {
       fmt.Println(err1)
   }

}
