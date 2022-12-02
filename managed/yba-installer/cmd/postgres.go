/*
 * Copyright (c) YugaByte, Inc.
 */

package cmd

import (
	"fmt"
	"os"
	"strings"

	"github.com/spf13/viper"

	"path/filepath"
	log "github.com/yugabyte/yugabyte-db/managed/yba-installer/logging"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/common"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/config"
)

// Component 1: Postgres
type Postgres struct {
	name                string
	SystemdFileLocation string
	ConfFileLocation    string
	templateFileName    string
	version             string
	MountPath						string
	dataDir							string
	PgBin								string
	LogFile							string
	cronScript					string
}

// NewPostgres creates a new postgres service struct at installRoot with specific version.
func NewPostgres(installRoot, version string) Postgres {
	return Postgres{
		"postgres",
		common.SystemdDir + "/postgres.service",
		common.InstallRoot + "/pgsql/conf",
		"yba-installer-postgres.yml",
		version,
		common.InstallRoot + "/pgsql/run/postgresql",
		common.InstallRoot + "/data/postgres",
		common.InstallRoot + "/pgsql/bin",
		common.InstallRoot + "/data/logs/postgres.log",
		fmt.Sprintf("%s/%s/managePostgres.sh", common.InstallVersionDir, common.CronDir)}
}

// TemplateFile returns service's templated config file path
func (pg Postgres) TemplateFile() string {
	return pg.templateFileName
}

// Name returns the name of the service.
func (pg Postgres) Name() string {
	return pg.name
}

// Install postgres and create the yugaware DB for YBA.
func (pg Postgres) Install() {
	config.GenerateTemplate(pg)
	pg.extractPostgresPackage()
	pg.runInitDB()
	pg.setUpDataDir()
	pg.modifyPostgresConf()
	pg.Start()
	pg.dropYugawareDatabase()
	pg.createYugawareDatabase()
	if !common.HasSudoAccess() {
		pg.CreateCronJob()
	}
}

// TODO: This should generate the correct start string based on installation mode
// and write it to the correct service file OR cron script.
// Start starts the postgres process either via systemd or cron script.
func (pg Postgres) Start() {

	if common.HasSudoAccess() {

		arg0 := []string{"daemon-reload"}
		common.ExecuteBashCommand(common.Systemctl, arg0)

		arg1 := []string{"enable", filepath.Base(pg.SystemdFileLocation)}
		common.ExecuteBashCommand(common.Systemctl, arg1)

		arg2 := []string{"restart", filepath.Base(pg.SystemdFileLocation)}
		common.ExecuteBashCommand(common.Systemctl, arg2)

		arg3 := []string{"status", filepath.Base(pg.SystemdFileLocation)}
		common.ExecuteBashCommand(common.Systemctl, arg3)

	} else {
		restartSeconds := config.GetYamlPathData("postgres.restartSeconds")

		command1 := "bash"
		arg1 := []string{"-c", pg.cronScript + " " + restartSeconds + " > /dev/null 2>&1 &"}

		common.ExecuteBashCommand(command1, arg1)

	}
}

// Stop stops the postgres process either via systemd or cron script.
func (pg Postgres) Stop() {

	if common.HasSudoAccess() {

		arg1 := []string{"stop", filepath.Base(pg.SystemdFileLocation)}
		common.ExecuteBashCommand(common.Systemctl, arg1)

	} else {

		// Delete the file used by the crontab bash script for monitoring.
		os.RemoveAll(common.InstallRoot + "/postgres/testfile")

		command1 := "bash"
		arg1 := []string{"-c",
			pg.PgBin + "/pg_ctl -D " + pg.ConfFileLocation +
				" -o \"-k " + pg.MountPath + "\" " +
				"-l " + pg.LogFile + " stop"}
		common.ExecuteBashCommand(command1, arg1)
	}

}

func (pg Postgres) Restart() {

	if common.HasSudoAccess() {

		arg1 := []string{"restart", filepath.Base(pg.SystemdFileLocation)}
		common.ExecuteBashCommand(common.Systemctl, arg1)

	} else {

		pg.Stop()
		pg.Start()

	}

}

// Uninstall drops the yugaware DB and removes Postgres binaries.
func (pg Postgres) Uninstall(removeData bool) {

	if removeData {
		// Drop yugaware DB
		pg.dropYugawareDatabase()
		// Remove data directory
		err := os.RemoveAll(pg.dataDir)
		if err != nil {
			log.Debug(fmt.Sprintf("Error %s removing postgres data dir %s.", err.Error(), pg.dataDir))
		}
	}

	// Remove conf/binary
	err := os.RemoveAll(filepath.Dir(pg.PgBin))
	if err != nil {
		log.Fatal(fmt.Sprintf("Error %s cleaning postgres binaries and conf %s",
			err.Error(), filepath.Dir(pg.PgBin)))
	}
}

func (pg Postgres) extractPostgresPackage() {

	// TODO: Replace with tar package
  command1 := "bash"
  arg1 := []string{"-c", "tar -zvxf " + common.BundledPostgresName + " -C " +
    common.InstallRoot}

  common.ExecuteBashCommand(command1, arg1)

	log.Debug(common.BundledPostgresName + " successfully extracted.")

}

func (pg Postgres) runInitDB() {

	common.Create(common.InstallRoot + "/data/logs/postgres.log")

	// Needed for socket acceptance in the non-root case.
	common.CreateDir(pg.MountPath, os.ModePerm)

	if common.HasSudoAccess() {

		// Need to give the yugabyte user ownership of the entire postgres
		// directory.
		common.Chown(filepath.Dir(pg.ConfFileLocation), "yugabyte", "yugabyte", true)
		common.Chown(filepath.Dir(pg.LogFile), "yugabyte", "yugabyte", true)

		command3 := "sudo"
		arg3 := []string{"-u", "yugabyte", "bash", "-c",
			pg.PgBin + "/initdb -U " + "yugabyte -D " + pg.ConfFileLocation}
		common.ExecuteBashCommand(command3, arg3)

	} else {

		currentUser := strings.ReplaceAll(strings.TrimSuffix(common.GetCurrentUser(), "\n"), " ", "")

		command1 := "bash"
		arg1 := []string{"-c",
			pg.PgBin + "/initdb -U " + currentUser + " -D " + pg.ConfFileLocation}
		common.ExecuteBashCommand(command1, arg1)

	}

}

// Set the data directory in postgresql.conf
func (pg Postgres) modifyPostgresConf() {
	// work to set data directory separate in postgresql.conf
	pgConfPath := pg.ConfFileLocation + "/postgresql.conf"
	confFile, err := os.OpenFile(pgConfPath, os.O_APPEND|os.O_WRONLY, 0600)
	if err != nil {
		log.Fatal(fmt.Sprintf("Error: %s reading file %s", err.Error(), pgConfPath))
	}
	defer confFile.Close()
	if _, err := confFile.WriteString(
			fmt.Sprintf("data_directory = '%s'\n", pg.dataDir)); err != nil {
		log.Fatal(fmt.Sprintf("Error: %s writing new data_directory to %s", err.Error(), pgConfPath))
	}

}

// Move required files from initdb to the new data directory
func (pg Postgres) setUpDataDir() {
	if common.HasSudoAccess() {
		// move init conf to data dir
		common.ExecuteBashCommand("sudo",
			[]string{"-u", "yugabyte", "mv", pg.ConfFileLocation, pg.dataDir})

		// move conf files back to conf location
		common.CreateDir(pg.ConfFileLocation, 0700)
		common.Chown(pg.ConfFileLocation, "yugabyte", "yugabyte", false)
		common.ExecuteBashCommand(
			"sudo",
			[]string{"-u", "yugabyte", "find", pg.dataDir, "-iname", "*.conf", "-exec", "mv", "{}",
				pg.ConfFileLocation, ";"})

	}
	// TODO: Need to figure on non-root case.
}

func (pg Postgres) createYugawareDatabase() {

	createdbString := pg.PgBin + "/createdb -h " + pg.MountPath + " yugaware"
	command2 := "sudo"
	arg2 := []string{"-u", "yugabyte", "bash", "-c", createdbString}

	if !common.HasSudoAccess() {

		command2 = "bash"
		arg2 = []string{"-c", createdbString}

	}

	_, err := common.ExecuteBashCommand(command2, arg2)
	if err != nil {
		log.Fatal(fmt.Sprintf("Could not create yugaware database. Failed with error %s", err.Error()))
	}

}

func (pg Postgres) dropYugawareDatabase() {
	dropdbString := pg.PgBin + "/dropdb -h " + pg.MountPath + " yugaware"
	// dropArgs := []string{"bash", "-c", dropdbString}
	var err error
	if common.HasSudoAccess() {
		_, err = common.ExecuteBashCommand("sudo",
			[]string{"-u", "yugabyte", "bash", "-c", dropdbString})
	} else {
		_, err = common.ExecuteBashCommand(dropdbString, []string{})
	}

	if err != nil {
		log.Info(fmt.Sprintf("Error %s dropping yugaware databse.", err.Error()))
	}
}

// TODO: replace with pg_ctl status
// Status prints the status output specific to Postgres.
func (pg Postgres) Status() {

	name := "postgres"
	port := config.GetYamlPathData("postgres.port")

	runningStatus := ""

	if common.HasSudoAccess() {

		args := []string{"is-active", name}
		runningStatus, _ = common.ExecuteBashCommand(common.Systemctl, args)

		runningStatus = strings.ReplaceAll(strings.TrimSuffix(runningStatus, "\n"), " ", "")

		// For display purposes.
		if runningStatus != "active" {

			runningStatus = "inactive"
		}

	} else {

		command := "bash"
		args := []string{"-c", "pgrep " + name}
		out0, _ := common.ExecuteBashCommand(command, args)

		if strings.TrimSuffix(string(out0), "\n") != "" {
			runningStatus = "active"
		} else {
			runningStatus = "inactive"
		}
	}

	systemdLoc := "N/A"

	if common.HasSudoAccess() {

		systemdLoc = pg.SystemdFileLocation
	}

	outString := name + "\t" + pg.version + "\t" + port +
		"\t" + pg.ConfFileLocation + "\t" +
		systemdLoc + "\t" + runningStatus + "\t"

	fmt.Fprintln(common.StatusOutput, outString)

}

// CreateCronJob creates the cron job for managing postgres with cron script in non-root.
func (pg Postgres) CreateCronJob() {
	restartSeconds := viper.GetString("postgres.port")
	common.ExecuteBashCommand("bash", []string{"-c",
		"(crontab -l 2>/dev/null; echo \"@reboot " + pg.cronScript + " " +
			restartSeconds + "\") | sort - | uniq - | crontab - "})
}
