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

/*

 Key points of postgres conf

 When we set it up ourselves
 1. It uses default listen_addresses setting to only bind to localhost
 2. No password is then setup because we don't expose this to outside hosts
 3. We always set up a superuser 'postgres'
 4. hba conf is at the default settings to trust host/local for user postgres (not require password)

 When user sets it up, they need to specify host, port, dbname, username, password.
 dbname must already exist.
 TODO: TLS enabled conns might trigger a preflight check, this can be skipped.
*/

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

		ConfFileLocation: common.GetSoftwareRoot() + "/pgsql/conf",
		// TODO: fix this (conf shd be in data dir or in its own dir)

		templateFileName: "yba-installer-postgres.yml",
		MountPath:        common.GetBaseInstall() + "/data/pgsql/run/postgresql",
		dataDir:          common.GetBaseInstall() + "/data/postgres",
		PgBin:            common.GetSoftwareRoot() + "/pgsql/bin",
		LogFile:          common.GetBaseInstall() + "/data/logs/postgres.log",
		cronScript: filepath.Join(
			common.GetInstallerSoftwareDir(), common.CronDir, "managePostgres.sh")}
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

func (pg Postgres) getPgUserName() string {
	return "postgres"
}

// Install postgres and create the yugaware DB for YBA.
func (pg Postgres) Install() {
	config.GenerateTemplate(pg)
	pg.extractPostgresPackage()

	// First let initdb create its config and data files in the software/pg../conf location
	pg.runInitDB()
	// Then copy over data files to the intended data dir location
	pg.setUpDataDir()
	// Finally update the conf file location to match this new data dir location
	pg.modifyPostgresConf()

	pg.Start()
	if viper.GetBool("postgres.install.enabled") {
		pg.createYugawareDatabase()
	}
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
		common.RunBash(common.Systemctl, arg0)

		arg1 := []string{"enable", filepath.Base(pg.SystemdFileLocation)}
		common.RunBash(common.Systemctl, arg1)

		arg2 := []string{"restart", filepath.Base(pg.SystemdFileLocation)}
		common.RunBash(common.Systemctl, arg2)

		arg3 := []string{"status", filepath.Base(pg.SystemdFileLocation)}
		common.RunBash(common.Systemctl, arg3)

	} else {
		restartSeconds := config.GetYamlPathData("postgres.restartSeconds")

		command1 := "bash"
		arg1 := []string{"-c", pg.cronScript + " " + restartSeconds + " > /dev/null 2>&1 &"}

		common.RunBash(command1, arg1)

	}
}

// Stop stops the postgres process either via systemd or cron script.
func (pg Postgres) Stop() {

	if common.HasSudoAccess() {

		arg1 := []string{"stop", filepath.Base(pg.SystemdFileLocation)}
		common.RunBash(common.Systemctl, arg1)

	} else {

		// Delete the file used by the crontab bash script for monitoring.
		common.RemoveAll(common.GetSoftwareRoot() + "/postgres/testfile")

		command1 := "bash"
		arg1 := []string{"-c",
			pg.PgBin + "/pg_ctl -D " + pg.ConfFileLocation +
				" -o \"-k " + pg.MountPath + "\" " +
				"-l " + pg.LogFile + " stop"}
		common.RunBash(command1, arg1)
	}

}

func (pg Postgres) Restart() {

	if common.HasSudoAccess() {

		arg1 := []string{"restart", filepath.Base(pg.SystemdFileLocation)}
		common.RunBash(common.Systemctl, arg1)

	} else {

		pg.Stop()
		pg.Start()

	}

}

// Uninstall drops the yugaware DB and removes Postgres binaries.
func (pg Postgres) Uninstall(removeData bool) {

	pg.Stop()

	if removeData {
		// Remove data directory
		// TODO: we should also remove the pgsql run directory
		err := common.RemoveAll(pg.dataDir)
		if err != nil {
			log.Debug(fmt.Sprintf("Error %s removing postgres data dir %s.", err.Error(), pg.dataDir))
		}
	}

	if common.HasSudoAccess() {
		err := os.Remove(pg.SystemdFileLocation)
		if err != nil {
			log.Info(fmt.Sprintf("Error %s removing systemd service %s.",
				err.Error(), pg.SystemdFileLocation))
		}

		// reload systemd daemon
		common.RunBash(common.Systemctl, []string{"daemon-reload"})
	}

	// Remove conf/binary
	err := common.RemoveAll(filepath.Dir(pg.PgBin))
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
		"-p", viper.GetString("postgres.install.port"),
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
	_, err := common.RunBash(psql, args)
	if err != nil {
		log.Fatal("postgres restore from backup failed: " + err.Error())
	}
	log.Debug("postgres restore from backup complete")
}

// UpgradeMajorVersion will upgrade postgres and install it into the alt install directory.
// Upgrade will NOT restart the service, the old version is expected to still be running
// This function should be primarily used for major version changes for postgres.
// TODO: we should gate this to only postgres.install.enabled = true
func (pg Postgres) UpgradeMajorVersion() {
	log.Info("Starting Postgres major upgrade")
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
	log.Info("Completed Postgres major upgrade")
}

// Upgrade will do a minor version upgrade of postgres
func (pg Postgres) Upgrade() {
	log.Info("Starting Postgres upgrade")
	pg.postgresDirectories = newPostgresDirectories()
	config.GenerateTemplate(pg) // NOTE: This does not require systemd reload, start does it for us.
	pg.extractPostgresPackage()
	pg.copyConfFiles()
	pg.modifyPostgresConf()

	if !common.HasSudoAccess() {
		pg.CreateCronJob()
	}

}

func (pg Postgres) extractPostgresPackage() {

	postgresPackagePath := common.GetPostgresPackagePath()

	// TODO: Replace with tar package
	command1 := "bash"
	arg1 := []string{"-c", "tar -zxf " + postgresPackagePath + " -C " +
		common.GetSoftwareRoot()}

	common.RunBash(command1, arg1)
}

func (pg Postgres) runInitDB() {

	common.Create(common.GetBaseInstall() + "/data/logs/postgres.log")
	// Needed for socket acceptance in the non-root case.
	common.MkdirAllOrFail(pg.MountPath, os.ModePerm)

	if common.HasSudoAccess() {

		// Need to give the yugabyte user ownership of the entire postgres
		// directory.
		userName := viper.GetString("service_username")

		common.Chown(filepath.Dir(pg.ConfFileLocation), userName, userName, true)
		common.Chown(filepath.Dir(pg.LogFile), userName, userName, true)
		common.Chown(pg.MountPath, userName, userName, true)

		command3 := "sudo"
		arg3 := []string{"-u", userName, "bash", "-c",
			pg.PgBin + "/initdb -U " + pg.getPgUserName() + " -D " + pg.ConfFileLocation +
				" --locale=" + viper.GetString("postgres.install.locale")}
		if _, err := common.RunBash(command3, arg3); err != nil {
			log.Fatal("Failed to run initdb for postgres: " + err.Error())
		}

	} else {

		command1 := "bash"
		arg1 := []string{"-c",
			pg.PgBin + "/initdb -U " + pg.getPgUserName() + " " + " -D " + pg.ConfFileLocation +
				" --locale=" + viper.GetString("postgres.install.locale")}
		if _, err := common.RunBash(command1, arg1); err != nil {
			log.Fatal("Failed to run initdb for postgres: " + err.Error())
		}
	}
}

// Set the data directory in postgresql.conf
func (pg Postgres) modifyPostgresConf() {
	// work to set data directory separate in postgresql.conf
	pgConfPath := filepath.Join(pg.ConfFileLocation, "postgresql.conf")
	confFile, err := os.OpenFile(pgConfPath, os.O_APPEND|os.O_WRONLY, 0600)
	if err != nil {
		log.Fatal(fmt.Sprintf("Error: %s reading file %s", err.Error(), pgConfPath))
	}
	defer confFile.Close()
	_, err = confFile.WriteString(
		fmt.Sprintf("data_directory = '%s'\n", pg.dataDir))
	if err != nil {
		log.Fatal(fmt.Sprintf("Error: %s writing new data_directory to %s", err.Error(), pgConfPath))
	}
}

// Move required files from initdb to the new data directory
func (pg Postgres) setUpDataDir() {
	if common.HasSudoAccess() {
		userName := viper.GetString("service_username")
		// move init conf to data dir
		_, err := common.RunBash("sudo",
			[]string{"-u", userName, "mv", pg.ConfFileLocation, pg.dataDir})
		if err != nil {
			log.Fatal("failed to move config: " + err.Error())
		}
		pg.copyConfFiles() // move conf files back to conf location
	}
	// TODO: Need to figure on non-root case.
}

func (pg Postgres) copyConfFiles() {
	// move conf files back to conf location
	userName := viper.GetString("service_username")

	common.MkdirAllOrFail(pg.ConfFileLocation, 0700)
	common.Chown(pg.ConfFileLocation, userName, userName, false)
	common.RunBash(
		"sudo",
		[]string{"-u", userName, "find", pg.dataDir, "-iname", "*.conf", "-exec", "cp", "{}",
			pg.ConfFileLocation, ";"})
}

func (pg Postgres) createYugawareDatabase() {

	createdbString := pg.PgBin + "/createdb -h " + pg.MountPath + " -U " + pg.getPgUserName() + " yugaware"
	command2 := "sudo"
	arg2 := []string{"-u", viper.GetString("service_username"), "bash", "-c", createdbString}

	if !common.HasSudoAccess() {

		command2 = "bash"
		arg2 = []string{"-c", createdbString}

	}

	_, err := common.RunBash(command2, arg2)
	if err != nil {
		if strings.Contains(err.Error(), "already exists") {
			// db already existing is fine because this may be a resumed failed install
			return
		}
		log.Fatal(fmt.Sprintf("Could not create yugaware database. Failed with error %s", err.Error()))
	}

}

// TODO: replace with pg_ctl status
// Status prints the status output specific to Postgres.
func (pg Postgres) Status() common.Status {
	status := common.Status{
		Service: pg.Name(),
		Port:    viper.GetInt("postgres.install.port"),
		Version: pg.version,
	}

	// User brought there own service, we don't know much about the status
	if viper.GetBool("postgres.useExisting.enabled") {
		status.Status = common.StatusUserOwned
		status.Port = viper.GetInt("postgres.useExisting.port")
		host := viper.GetString("postgres.useExisting.host")
		if host == "" {
			host = "localhost"
		}
		status.Hostname = host
		status.Version = "Unknown"
		return status
	}

	status.ConfigLoc = pg.ConfFileLocation
	status.LogFileLoc = pg.postgresDirectories.LogFile

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
		out0, _ := common.RunBash(command, args)

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
	restartSeconds := viper.GetString("postgres.install.restartSeconds")
	common.RunBash("bash", []string{"-c",
		"(crontab -l 2>/dev/null; echo \"@reboot " + pg.cronScript + " " +
			restartSeconds + "\") | sort - | uniq - | crontab - "})
}
