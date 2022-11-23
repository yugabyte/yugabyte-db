/*
 * Copyright (c) YugaByte, Inc.
 */

package cmd

import (
	"fmt"
	"os"
	"strings"

	// "time"
	"path/filepath"

	"github.com/spf13/viper"
)

// Component 1: Postgres
type Postgres struct {
	Name                string
	SystemdFileLocation string
	ConfFileLocation    string
	templateFileName    string
	Version             string
	MountPath           string
	dataDir             string
	PgCtl               string
	LogFile             string
}

func NewPostgres(installRoot, version string) Postgres {
	return Postgres{
		"postgres",
		SYSTEMD_DIR + "/postgres.service",
		INSTALL_ROOT + "/postgres/conf",
		"yba-installer-postgres.yml",
		version,
		INSTALL_ROOT + "/postgres/run/postgresql",
		INSTALL_ROOT + "/data/postgres",
		INSTALL_ROOT + "/postgres/bin/pg_ctl",
		INSTALL_ROOT + "/data/logs/postgres.log"}
}

// var MountPath = INSTALL_ROOT + "/postgres/run/postgresql"
// var pgDataDir = INSTALL_ROOT + "/data/postgres"
// var pgConfDir = INSTALL_ROOT + "/postgres/conf"

// Method of the Component
// Interface are implemented by
// the Postgres struct and customizable
// for each specific service.

func (pg Postgres) SetUpPrereqs() {
	extractPostgresPackage()
}

func extractPostgresPackage() {

	// TODO: Replace with tar package
	command1 := "bash"
	arg1 := []string{"-c", "tar -zvxf " + bundledPostgresName + " -C " +
		INSTALL_ROOT}

	ExecuteBashCommand(command1, arg1)

	// TODO: I don't think we need this if we move data dirs to "INSTALL_ROOT/data/postgres"
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
	pg.runInitDB()
	// time.Sleep(5 * time.Second)
	pg.setUpDataDir()
	pg.modifyPostgresConf()
	pg.Start()
	pg.createYugawareDatabase()
	if !hasSudoAccess() {
		pg.CreateCronJob()
	}
}

// TODO: This should generate the correct start string based on installation mode
// and write it to the correct service file OR cron script.
// Start starts the postgres process either via systemd or cron script.
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
		restartSeconds := fmt.Sprintf("%d", viper.GetInt("postgres.restartSeconds"))
		scriptPath := INSTALL_VERSION_DIR + "/crontabScripts/manage" + pg.Name + "NonRoot.sh"

		command1 := "bash"
		arg1 := []string{"-c", scriptPath + " " + restartSeconds + " > /dev/null 2>&1 &"}

		ExecuteBashCommand(command1, arg1)

	}
}

// Stop stops the postgres process either via systemd or cron script.
func (pg Postgres) Stop() {

	if hasSudoAccess() {

		arg1 := []string{"stop", filepath.Base(pg.SystemdFileLocation)}
		ExecuteBashCommand(SYSTEMCTL, arg1)

	} else {

		// Delete the file used by the crontab bash script for monitoring.
		os.RemoveAll(INSTALL_ROOT + "/postgres/testfile")

		command1 := "bash"
		arg1 := []string{"-c",
			pg.PgCtl + " -D " + pg.ConfFileLocation +
				" -o \"-k " + pg.MountPath + "\" " +
				"-l " + INSTALL_ROOT + "/data/logs/postgres.log stop"}
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

func (pg Postgres) getSystemdFile() string {
	return pg.SystemdFileLocation
}

func (pg Postgres) getConfFile() string {
	return pg.ConfFileLocation
}

func (pg Postgres) getTemplateFile() string {
	return pg.templateFileName
}

// Uninstall drops the yugaware DB and removes Postgres binaries.
func (pg Postgres) Uninstall(removeData bool) {

	dropdbString := INSTALL_ROOT + "/postgres/bin/dropdb -h" + pg.MountPath + " yugaware"

	command1 := "sudo"
	arg1 := []string{"-u", "yugabyte", "bash", "-c", dropdbString}

	if !hasSudoAccess() {

		command1 = "bash"
		arg1 = []string{"-c", dropdbString}

	}

	ExecuteBashCommand(command1, arg1)
	RemoveAllExceptDataVolumes([]string{"postgres"})

}

// VersionInfo returns the postgres version.
func (pg Postgres) VersionInfo() string {
	return pg.Version
}

func (pg Postgres) runInitDB() {

	os.Create(INSTALL_ROOT + "/data/logs/postgres.log")

	// Needed for socket acceptance in the non-root case.
	CreateDir(pg.MountPath, os.ModePerm)

	if hasSudoAccess() {

		// Need to give the yugabyte user ownership of the entire postgres
		// directory.
		Chown(INSTALL_ROOT+"/postgres/", "yugabyte", "yugabyte", true)
		Chown(INSTALL_ROOT+"/data/logs/", "yugabyte", "yugabyte", true)

		command3 := "sudo"
		arg3 := []string{"-u", "yugabyte", "bash", "-c",
			INSTALL_ROOT + "/postgres/bin/initdb -U " + "yugabyte -D " + pg.ConfFileLocation}
		ExecuteBashCommand(command3, arg3)

	} else {

		currentUser, _ := ExecuteBashCommand("bash", []string{"-c", "whoami"})
		currentUser = strings.ReplaceAll(strings.TrimSuffix(currentUser, "\n"), " ", "")

		command1 := "bash"
		arg1 := []string{"-c",
			INSTALL_ROOT + "/postgres/bin/initdb -U " + currentUser + " -D " + pg.ConfFileLocation}
		ExecuteBashCommand(command1, arg1)

	}

}

// Set the data directory in postgresql.conf
func (pg Postgres) modifyPostgresConf() {
	// work to set data directory separate in postgresql.conf
	pgConfPath := pg.ConfFileLocation + "/postgresql.conf"
	confFile, err := os.OpenFile(pgConfPath, os.O_APPEND|os.O_WRONLY, 0600)
	if err != nil {
		LogError(fmt.Sprintf("Error: %s reading file %s", err.Error(), pgConfPath))
	}
	defer confFile.Close()
	if _, err := confFile.WriteString(fmt.Sprintf("data_directory = '%s'\n", pg.dataDir)); err != nil {
		LogError(fmt.Sprintf("Error: %s writing to %s", err.Error(), pgConfPath))
	}

}

// Move required files from initdb to the new data directory
func (pg Postgres) setUpDataDir() {
	if hasSudoAccess() {
		// move init conf to data dir
		ExecuteBashCommand("sudo", []string{"-u", "yugabyte", "mv", pg.ConfFileLocation, pg.dataDir})

		// move conf files back to conf location
		CreateDir(pg.ConfFileLocation, 0700)
		Chown(pg.ConfFileLocation, "yugabyte", "yugabyte", false)
		ExecuteBashCommand(
			"sudo",
			[]string{"-u", "yugabyte", "find", pg.dataDir, "-iname", "*.conf", "-exec", "mv", "{}",
				pg.ConfFileLocation, ";"})

	}
}

func (pg Postgres) createYugawareDatabase() {

	dropdbString := INSTALL_ROOT + "/postgres/bin/dropdb -h " + pg.MountPath + " yugaware"
	createdbString := INSTALL_ROOT + "/postgres/bin/createdb -h " + pg.MountPath + " yugaware"

	command1 := "sudo"
	arg1 := []string{"-u", "yugabyte", "bash", "-c", dropdbString}

	if !hasSudoAccess() {

		command1 = "bash"
		arg1 = []string{"-c", dropdbString}

	}

	_, err1 := ExecuteBashCommand(command1, arg1)

	if err1 != nil {
		LogDebug("Yugaware database doesn't exist yet, skipping drop.")
	}

	command2 := "sudo"
	arg2 := []string{"-u", "yugabyte", "bash", "-c", createdbString}

	if !hasSudoAccess() {

		command2 = "bash"
		arg2 = []string{"-c", createdbString}

	}

	ExecuteBashCommand(command2, arg2)
}

// TODO: replace with pg_ctl status
// Status prints the status output specific to Postgres.
func (pg Postgres) Status() {

	name := "postgres"
	port := fmt.Sprintf("%d", viper.GetInt("postgres.port"))

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
		"\t" + pg.ConfFileLocation + "\t" +
		systemdLoc + "\t" + runningStatus + "\t"

	fmt.Fprintln(statusOutput, outString)

}

// CreateCronJob sets up the cron script in the cron tab.
func (pg Postgres) CreateCronJob() {
	restartSeconds := fmt.Sprintf("%d", viper.GetInt("postgres.port"))
	scriptPath := INSTALL_VERSION_DIR + "/crontabScripts/manage" + pg.Name + "NonRoot.sh"
	ExecuteBashCommand("bash", []string{"-c",
		"(crontab -l 2>/dev/null; echo \"@reboot " + scriptPath + " " +
			restartSeconds + "\") | sort - | uniq - | crontab - "})
}
