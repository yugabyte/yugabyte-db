/*
 * Copyright (c) YugaByte, Inc.
 */

package cmd

import (
	"errors"
	"fmt"
	"os"
	"os/exec"
	"strings"

	"github.com/spf13/viper"

	"path/filepath"

	"github.com/yugabyte/yugabyte-db/managed/yba-installer/common"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/config"
	log "github.com/yugabyte/yugabyte-db/managed/yba-installer/logging"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/systemd"
)

type postgresDirectories struct {
	SystemdFileLocation string
	ConfFileLocation    string
	templateFileName    string
	MountPath           string
	dataDir             string
	PgBin               string
	LogFile             string
	cronScript          string
}

func newPostgresDirectories() postgresDirectories {
	return postgresDirectories{
		SystemdFileLocation: common.SystemdDir + "/postgres.service",
		ConfFileLocation:    common.GetInstallRoot() + "/pgsql/conf",
		templateFileName:    "yba-installer-postgres.yml",
		MountPath:           common.GetBaseInstall() + "/data/pgsql/run/postgresql",
		dataDir:             common.GetBaseInstall() + "/data/postgres",
		PgBin:               common.GetInstallRoot() + "/pgsql/bin",
		LogFile:             common.GetBaseInstall() + "/data/logs/postgres.log",
		cronScript: filepath.Join(
			common.GetInstallVersionDir(), common.CronDir, "managePostgres.sh")}
}

// Component 1: Postgres
type Postgres struct {
	name    string
	version string
	postgresDirectories
}

// NewPostgres creates a new postgres service struct at installRoot with specific version.
func NewPostgres(version string) Postgres {
	return Postgres{
		name:                "postgres",
		version:             version,
		postgresDirectories: newPostgresDirectories(),
	}
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
	log.Info("Starting Postgres install")
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
	log.Info("Finishing Postgres install")
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
		os.RemoveAll(common.GetInstallRoot() + "/postgres/testfile")

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

func (pg Postgres) CreateBackup() {
	log.Debug("starting postgres backup")
	outFile := filepath.Join(common.GetBaseInstall(), "data", "postgres_backup")
	if _, err := os.Stat(outFile); !errors.Is(err, os.ErrNotExist) {
		os.Remove(outFile)
	}
	file, err := os.Create(outFile)
	if err != nil {
		log.Fatal("faled to open file " + outFile + ": " + err.Error())
	}
	defer file.Close()

	// We want the active install directory even during the upgrade workflow.
	pg_dumpall := filepath.Join(common.GetActiveSymlink(), "/pgsql/bin/pg_dumpall")
	args := []string{
		"-p", viper.GetString("postgres.port"),
		"-h", "localhost",
		"-U", viper.GetString("service_username"),
	}
	cmd := exec.Command(pg_dumpall, args...)
	cmd.Stdout = file
	if err := cmd.Run(); err != nil {
		log.Fatal("postgres backup failed: " + err.Error())
	}
	log.Debug("postgres backup comlete")
}

func (pg Postgres) RestoreBackup() {
	log.Debug("postgres starting restore from backup")
	inFile := filepath.Join(common.GetBaseInstall(), "data", "postgres_backup")
	if _, err := os.Stat(inFile); errors.Is(err, os.ErrNotExist) {
		log.Fatal("backup file does not exist")
	}

	psql := filepath.Join(pg.PgBin, "psql")
	args := []string{
		"-d", "postgres",
		"-f", inFile,
		"-h", "localhost",
		"-p", viper.GetString("postgres.port"),
		"-U", viper.GetString("service_username"),
	}
	_, err := common.ExecuteBashCommand(psql, args)
	if err != nil {
		log.Fatal("postgres restore from backup failed: " + err.Error())
	}
	log.Debug("postgres restore from backup complete")
}

// UpgradeMajorVersion will upgrade postgres and install it into the alt install directory.
// Upgrade will NOT restart the service, the old version is expected to still be running
// This function should be primarily used for major version changes for postgres.
func (pg Postgres) UpgradeMajorVersion() {
	log.Info("Starting Postgres upgrade")
	pg.CreateBackup()
	pg.Stop()
	pg.postgresDirectories = newPostgresDirectories()
	config.GenerateTemplate(pg) // NOTE: This does not require systemd reload, start does it for us.
	pg.extractPostgresPackage()
	pg.runInitDB()
	pg.setUpDataDir()
	pg.modifyPostgresConf()
	pg.Start()
	pg.RestoreBackup()

	if !common.HasSudoAccess() {
		pg.CreateCronJob()
	}
	log.Info("Finishing Postgres upgrade")
}

// Upgrade will do a minor version upgrade of postgres
func (pg Postgres) Upgrade() {
	log.Info("Starting Postgres upgrade")
	pg.postgresDirectories = newPostgresDirectories()
	config.GenerateTemplate(pg) // NOTE: This does not require systemd reload, start does it for us.
	pg.extractPostgresPackage()
	pg.moveConfFiles()
	pg.modifyPostgresConf()

	if !common.HasSudoAccess() {
		pg.CreateCronJob()
	}

	log.Info("Finishing Postgres upgrade")
}

func (pg Postgres) extractPostgresPackage() {

	// TODO: Replace with tar package
	command1 := "bash"
	arg1 := []string{"-c", "tar -zvxf " + common.BundledPostgresName + " -C " +
		common.GetInstallRoot()}

	common.ExecuteBashCommand(command1, arg1)

	log.Debug(common.BundledPostgresName + " successfully extracted.")

}

func (pg Postgres) runInitDB() {

	common.Create(common.GetBaseInstall() + "/data/logs/postgres.log")
	// Needed for socket acceptance in the non-root case.
	common.CreateDir(pg.MountPath, os.ModePerm)

	if common.HasSudoAccess() {

		// Need to give the yugabyte user ownership of the entire postgres
		// directory.
		userName := viper.GetString("service_username")

		common.Chown(filepath.Dir(pg.ConfFileLocation), userName, userName, true)
		common.Chown(filepath.Dir(pg.LogFile), userName, userName, true)
		common.Chown(pg.MountPath, userName, userName, true)

		command3 := "sudo"
		arg3 := []string{"-u", userName, "bash", "-c",
			pg.PgBin + "/initdb -U " + userName + " -D " + pg.ConfFileLocation}
		if _, err := common.ExecuteBashCommand(command3, arg3); err != nil {
			log.Fatal("Failed to run initdb for postgres: " + err.Error())
		}

	} else {

		currentUser := strings.ReplaceAll(strings.TrimSuffix(common.GetCurrentUser(), "\n"), " ", "")

		command1 := "bash"
		arg1 := []string{"-c",
			pg.PgBin + "/initdb -U " + currentUser + " -D " + pg.ConfFileLocation}
		if _, err := common.ExecuteBashCommand(command1, arg1); err != nil {
			log.Fatal("Failed to run initdb for postgres: " + err.Error())
		}
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
		userName := viper.GetString("service_username")
		// move init conf to data dir
		_, err := common.ExecuteBashCommand("sudo",
			[]string{"-u", userName, "mv", pg.ConfFileLocation, pg.dataDir})
		if err != nil {
			log.Fatal("failed to move config: " + err.Error())
		}
		pg.moveConfFiles() // move conf files back to conf location
	}
	// TODO: Need to figure on non-root case.
}

func (pg Postgres) moveConfFiles() {
	// move conf files back to conf location
	userName := viper.GetString("service_username")

	common.CreateDir(pg.ConfFileLocation, 0700)
	common.Chown(pg.ConfFileLocation, userName, userName, false)
	common.ExecuteBashCommand(
		"sudo",
		[]string{"-u", userName, "find", pg.dataDir, "-iname", "*.conf", "-exec", "cp", "{}",
			pg.ConfFileLocation, ";"})
}

func (pg Postgres) createYugawareDatabase() {

	createdbString := pg.PgBin + "/createdb -h " + pg.MountPath + " yugaware"
	command2 := "sudo"
	arg2 := []string{"-u", viper.GetString("service_username"), "bash", "-c", createdbString}

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
			[]string{"-u", viper.GetString("service_username"), "bash", "-c", dropdbString})
	} else {
		_, err = common.ExecuteBashCommand(dropdbString, []string{})
	}

	if err != nil {
		log.Info(fmt.Sprintf("Error %s dropping yugaware databse.", err.Error()))
	}
}

// TODO: replace with pg_ctl status
// Status prints the status output specific to Postgres.
func (pg Postgres) Status() common.Status {
	status := common.Status{
		Service:   pg.Name(),
		Port:      viper.GetInt("postgres.port"),
		Version:   pg.version,
		ConfigLoc: pg.ConfFileLocation,
	}

	// Set the systemd service file location if one exists
	if common.HasSudoAccess() {
		status.ServiceFileLoc = pg.SystemdFileLocation
	} else {
		status.ServiceFileLoc = "N/A"
	}

	// Get the service status
	if common.HasSudoAccess() {
		props := systemd.Show(filepath.Base(pg.SystemdFileLocation), "LoadState", "SubState",
			"ActiveState")
		if props["LoadState"] == "not-found" {
			status.Status = common.StatusNotInstalled
		} else if props["SubState"] == "running" {
			status.Status = common.StatusRunning
		} else if props["ActiveState"] == "inactive" {
			status.Status = common.StatusStopped
		} else {
			status.Status = common.StatusErrored
		}
	} else {
		command := "bash"
		args := []string{"-c", "pgrep postgres"}
		out0, _ := common.ExecuteBashCommand(command, args)

		if strings.TrimSuffix(string(out0), "\n") != "" {
			status.Status = common.StatusRunning
		} else {
			status.Status = common.StatusStopped
		}
	}
	return status
}

// CreateCronJob creates the cron job for managing postgres with cron script in non-root.
func (pg Postgres) CreateCronJob() {
	restartSeconds := viper.GetString("postgres.port")
	common.ExecuteBashCommand("bash", []string{"-c",
		"(crontab -l 2>/dev/null; echo \"@reboot " + pg.cronScript + " " +
			restartSeconds + "\") | sort - | uniq - | crontab - "})
}
