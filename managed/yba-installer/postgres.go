/*
 * Copyright (c) YugaByte, Inc.
 */

package main

import (
	//"bytes"
	"fmt"
	//"io/ioutil"
	"os"
	"strings"
	//"log"
	"path/filepath"
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
	extractPostgresPackage()
}

func extractPostgresPackage() {

	command1 := "bash"
	arg1 := []string{"-c", "tar -zvxf " + bundledPostgresName + " -C " +
		INSTALL_ROOT}

	ExecuteBashCommand(command1, arg1)

	// Retain data volumes if it doesn't exist.
	if _, err := os.Stat(INSTALL_ROOT + "/postgres"); os.IsNotExist(err) {

		MoveFileGolang(INSTALL_ROOT+"/pgsql", INSTALL_ROOT+"/postgres")

	} else {

		// Move data directory back in prior to renaming.
		os.RemoveAll(INSTALL_ROOT + "/pgsql/data")

		MoveFileGolang(INSTALL_ROOT+"/postgres/data", INSTALL_ROOT+"/pgsql/data")

		os.RemoveAll(INSTALL_ROOT + "/postgres")

		MoveFileGolang(INSTALL_ROOT+"/pgsql", INSTALL_ROOT+"/postgres")

	}

}

func (pg Postgres) Install() {
	runInitDB()
	pg.Start()
	// Enough time to create the Yugaware Database.
	if !hasSudoAccess() {
		time.Sleep(5 * time.Second)
	}
	createYugawareDatabase()
	if !hasSudoAccess() {
		pg.CreateCronJob()
	}
}

func (pg Postgres) Start() {

	if hasSudoAccess() {

		arg0 := []string{"daemon-reload"}
		ExecuteBashCommand(SYSTEMCTL, arg0)

		arg1 := []string{"enable", filepath.Base(pg.SystemdFileLocation)}
		ExecuteBashCommand(SYSTEMCTL, arg1)

		arg2 := []string{"restart", filepath.Base(pg.SystemdFileLocation)}
		ExecuteBashCommand(SYSTEMCTL, arg2)

		arg3 := []string{"status", filepath.Base(pg.SystemdFileLocation)}
		ExecuteBashCommand(SYSTEMCTL, arg3)

	} else {

		scriptPath := INSTALL_VERSION_DIR + "/crontabScripts/manage" + pg.Name + "NonRoot.sh"

		command1 := "bash"
		arg1 := []string{"-c", scriptPath + " > /dev/null 2>&1 &"}

		ExecuteBashCommand(command1, arg1)

	}
}

func (pg Postgres) Stop() {

	if hasSudoAccess() {

		arg1 := []string{"stop", filepath.Base(pg.SystemdFileLocation)}
		ExecuteBashCommand(SYSTEMCTL, arg1)

	} else {

		// Delete the file used by the crontab bash script for monitoring.
		os.RemoveAll(INSTALL_ROOT + "/postgres/testfile")

		mountPath := INSTALL_ROOT + "/postgres/run/postgresql/"

		command1 := "bash"
		arg1 := []string{"-c",
			INSTALL_ROOT + "/postgres/bin/pg_ctl -D " + INSTALL_ROOT + "/postgres/data " +
				"-o \"-k " + mountPath + "\" " +
				"-l " + INSTALL_ROOT + "/postgres/logfile stop"}
		ExecuteBashCommand(command1, arg1)
	}

}

func (pg Postgres) Restart() {

	if hasSudoAccess() {

		arg1 := []string{"restart", filepath.Base(pg.SystemdFileLocation)}
		ExecuteBashCommand(SYSTEMCTL, arg1)

	} else {

		pg.Stop()
		pg.Start()

	}

}

func (pg Postgres) GetSystemdFile() string {
	return pg.SystemdFileLocation
}

func (pg Postgres) GetConfFile() []string {
	return pg.ConfFileLocation
}

// Per current cleanup.sh script.
func (pg Postgres) Uninstall() {

	mountPath := INSTALL_ROOT + "/postgres/run/postgresql/"

	dropdbString := INSTALL_ROOT + "/postgres/bin/dropdb -h" + mountPath + " yugaware"

	command1 := "sudo"
	arg1 := []string{"-u", "postgres", "bash", "-c", dropdbString}

	if !hasSudoAccess() {

		command1 = "bash"
		arg1 = []string{"-c", dropdbString}

	}

	ExecuteBashCommand(command1, arg1)
	RemoveAllExceptDataVolumes([]string{"postgres"})

}

func (pg Postgres) VersionInfo() string {
	return pg.Version
}

func runInitDB() {

	os.Create(INSTALL_ROOT + "/postgres/logfile")

	// Needed for socket acceptance in the non-root case.
	os.MkdirAll(INSTALL_ROOT+"/postgres/run/postgresql", os.ModePerm)

	if hasSudoAccess() {

		commandCheck := "bash"
		argsCheck := []string{"-c", "id -u postgres"}
		_, err := ExecuteBashCommand(commandCheck, argsCheck)

		if err != nil {

			commandUser := "useradd"
			argUser := []string{"--no-create-home", "--shell", "/bin/false", "postgres"}
			ExecuteBashCommand(commandUser, argUser)

		} else {
			LogDebug("User postgres already exists, skipping user creation.")
		}

		// Need to give the postgres user ownership of the entire postgres
		// directory.

		ExecuteBashCommand("chown",
			[]string{"-R", "postgres:postgres", INSTALL_ROOT + "/postgres/"})

		command3 := "sudo"
		arg3 := []string{"-u", "postgres", "bash", "-c",
			INSTALL_ROOT + "/postgres/bin/initdb -U " + "postgres -D " +
				INSTALL_ROOT + "/postgres/data"}
		ExecuteBashCommand(command3, arg3)

	} else {

		currentUser, _ := ExecuteBashCommand("bash", []string{"-c", "whoami"})
		currentUser = strings.ReplaceAll(strings.TrimSuffix(currentUser, "\n"), " ", "")

		command1 := "bash"
		arg1 := []string{"-c",
			INSTALL_ROOT + "/postgres/bin/initdb -U " + currentUser + " -D " +
				INSTALL_ROOT + "/postgres/data"}
		ExecuteBashCommand(command1, arg1)

	}

}

func createYugawareDatabase() {

	mountPath := INSTALL_ROOT + "/postgres/run/postgresql/"

	dropdbString := INSTALL_ROOT + "/postgres/bin/dropdb -h" + mountPath + " yugaware"
	createdbString := INSTALL_ROOT + "/postgres/bin/createdb -h " + mountPath + " yugaware"

	command1 := "sudo"
	arg1 := []string{"-u", "postgres", "bash", "-c", dropdbString}

	if !hasSudoAccess() {

		command1 = "bash"
		arg1 = []string{"-c", dropdbString}

	}

	_, err1 := ExecuteBashCommand(command1, arg1)

	if err1 != nil {
		LogDebug("Yugaware database doesn't exist yet, skipping drop.")
	}

	command2 := "sudo"
	arg2 := []string{"-u", "postgres", "bash", "-c", createdbString}

	if !hasSudoAccess() {

		command2 = "bash"
		arg2 = []string{"-c", createdbString}

	}

	ExecuteBashCommand(command2, arg2)
}

// Status prints the status output specific to Postgres.
func (pg Postgres) Status() {

	name := "postgres"
	port := getYamlPathData(".platform.externalPort")

	runningStatus := ""

	if hasSudoAccess() {

		args := []string{"is-active", name}
		runningStatus, _ = ExecuteBashCommand(SYSTEMCTL, args)

		runningStatus = strings.ReplaceAll(strings.TrimSuffix(runningStatus, "\n"), " ", "")

		// For display purposes.
		if runningStatus != "active" {

			runningStatus = "inactive"
		}

	} else {

		command := "bash"
		args := []string{"-c", "pgrep " + name}
		out0, _ := ExecuteBashCommand(command, args)

		if strings.TrimSuffix(string(out0), "\n") != "" {
			runningStatus = "active"
		} else {
			runningStatus = "inactive"
		}
	}

	systemdLoc := "N/A"

	if hasSudoAccess() {

		systemdLoc = pg.SystemdFileLocation
	}

	outString := name + "\t" + pg.Version + "\t" + port +
		"\t" + pg.ConfFileLocation[0] + "\t" +
		systemdLoc + "\t" + runningStatus + "\t"

	fmt.Fprintln(statusOutput, outString)

}

func (pg Postgres) CreateCronJob() {
	scriptPath := INSTALL_VERSION_DIR + "/crontabScripts/manage" + pg.Name + "NonRoot.sh"
	ExecuteBashCommand("bash", []string{"-c",
		"(crontab -l 2>/dev/null; echo \"@reboot " + scriptPath + "\") | sort - | uniq - | crontab - "})
}
