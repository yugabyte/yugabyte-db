/*
 * Copyright (c) YugaByte, Inc.
 */

package cmd

import (
	"errors"
	"fmt"
	"io/fs"
	"os"
	"strings"
	"time"

	"github.com/spf13/viper"

	"path/filepath"

	"github.com/yugabyte/yugabyte-db/managed/yba-installer/common"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/common/shell"
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
func (pg Postgres) Install() error {
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
	return nil
}

// TODO: This should generate the correct start string based on installation mode
// and write it to the correct service file OR cron script.
// Start starts the postgres process either via systemd or cron script.
func (pg Postgres) Start() error {

	if common.HasSudoAccess() {

		if out := shell.Run(common.Systemctl, "daemon-reload"); !out.SucceededOrLog() {
			return out.Error
		}

		if out := shell.Run(common.Systemctl, "enable",
			filepath.Base(pg.SystemdFileLocation)); !out.SucceededOrLog() {
			return out.Error
		}

		if out := shell.Run(common.Systemctl, "start",
			filepath.Base(pg.SystemdFileLocation)); !out.SucceededOrLog() {
			return out.Error
		}

		if out := shell.Run(common.Systemctl, "status",
			filepath.Base(pg.SystemdFileLocation)); !out.SucceededOrLog() {
			return out.Error
		}
	} else {
		restartSeconds := config.GetYamlPathData("postgres.install.restartSeconds")

		shell.RunShell(pg.cronScript, common.GetSoftwareRoot(), common.GetDataRoot(), restartSeconds,
			"> /dev/null 2>&1 &")
		// Non-root script does not wait for postgres to start, so check for it to be running.
		for {
			status, err := pg.Status()
			if err != nil {
				return err
			}
			if status.Status == common.StatusRunning {
				break
			}
			log.Info("waiting for non-root script to start postgres...")
			time.Sleep(time.Second * 2)
		}
	}
	return nil
}

// Stop stops the postgres process either via systemd or cron script.
func (pg Postgres) Stop() error {
	status, err := pg.Status()
	if err != nil {
		return err
	}
	if status.Status != common.StatusRunning {
		log.Debug(pg.name + " is already stopped")
		return nil
	}

	if common.HasSudoAccess() {
		out := shell.Run(common.Systemctl, "stop", filepath.Base(pg.SystemdFileLocation))
		if !out.SucceededOrLog() {
			return out.Error
		}
	} else {
		// Delete the file used by the crontab bash script for monitoring.
		common.RemoveAll(common.GetSoftwareRoot() + "/postgres/testfile")
		out := shell.Run(pg.PgBin+"/pg_ctl", "-D", pg.ConfFileLocation, "-o", "\"-k "+pg.MountPath+"\"",
			"-l", pg.LogFile, "stop")
		if !out.SucceededOrLog() {
			return out.Error
		}
	}
	return nil
}

func (pg Postgres) Restart() error {
	log.Info("Restarting postgres..")

	if common.HasSudoAccess() {
		if out := shell.Run(common.Systemctl, "restart",
			filepath.Base(pg.SystemdFileLocation)); !out.SucceededOrLog() {
			return out.Error
		}
	} else {
		if err := pg.Stop(); err != nil {
			return err
		}
		if err := pg.Start(); err != nil {
			return err
		}
	}
	return nil
}

// Uninstall drops the yugaware DB and removes Postgres binaries.
func (pg Postgres) Uninstall(removeData bool) error {
	log.Info("Uninstalling postgres")
	if err := pg.Stop(); err != nil {
		return err
	}

	if removeData {
		// Remove data directory
		if err := common.RemoveAll(pg.dataDir); err != nil {
			log.Info(fmt.Sprintf("Error %s removing postgres data dir %s.", err.Error(), pg.dataDir))
			return err
		}
	}

	if common.HasSudoAccess() {
		err := os.Remove(pg.SystemdFileLocation)
		if err != nil {
			pe := err.(*fs.PathError)
			if !errors.Is(pe.Err, fs.ErrNotExist) {
				log.Info(fmt.Sprintf("Error %s removing systemd service %s.",
					err.Error(), pg.SystemdFileLocation))
				return err
			}
		}

		// reload systemd daemon
		if out := shell.Run(common.Systemctl, "daemon-reload"); !out.SucceededOrLog() {
			return out.Error
		}
	}
	return nil
}

func (pg Postgres) CreateBackup() {
	log.Debug("starting postgres backup")
	outFile := filepath.Join(common.GetBaseInstall(), "data", "postgres_backup")
	if _, err := os.Stat(outFile); !errors.Is(err, os.ErrNotExist) {
		os.Remove(outFile)
	}
	file, err := os.Create(outFile)
	if err != nil {
		log.Fatal("failed to open file " + outFile + ": " + err.Error())
	}
	defer file.Close()

	// We want the active install directory even during the upgrade workflow.
	pg_dumpall := filepath.Join(common.GetActiveSymlink(), "/pgsql/bin/pg_dumpall")
	args := []string{
		"-p", viper.GetString("postgres.install.port"),
		"-h", "localhost",
		"-U", viper.GetString("service_username"),
	}
	out := shell.Run(pg_dumpall, args...)
	if !out.SucceededOrLog() {
		log.Fatal("postgres backup failed: " + out.Error.Error())
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
	out := shell.Run(psql, args...)
	if !out.SucceededOrLog() {
		log.Fatal("postgres restore from backup failed: " + out.Error.Error())
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
func (pg Postgres) Upgrade() error {
	log.Info("Starting Postgres upgrade")
	pg.postgresDirectories = newPostgresDirectories()
	config.GenerateTemplate(pg) // NOTE: This does not require systemd reload, start does it for us.
	pg.extractPostgresPackage()
	pg.copyConfFiles()
	pg.modifyPostgresConf()

	if !common.HasSudoAccess() {
		pg.CreateCronJob()
	}
	return nil
}

func (pg Postgres) extractPostgresPackage() {
	postgresPackagePath := common.GetPostgresPackagePath()
	shell.Run("tar", "-zxf", postgresPackagePath, "-C", common.GetSoftwareRoot())
}

func (pg Postgres) runInitDB() {

	common.Create(common.GetBaseInstall() + "/data/logs/postgres.log")
	// Needed for socket acceptance in the non-root case.
	common.MkdirAllOrFail(pg.MountPath, os.ModePerm)

	cmdName := pg.PgBin + "/initdb"
	initDbArgs := []string{
		"-U",
		pg.getPgUserName(),
		"-D",
		pg.ConfFileLocation,
		"--locale=" + viper.GetString("postgres.install.locale"),
	}
	if common.HasSudoAccess() {

		// Need to give the yugabyte user ownership of the entire postgres
		// directory.
		userName := viper.GetString("service_username")

		common.Chown(filepath.Dir(pg.ConfFileLocation), userName, userName, true)
		common.Chown(filepath.Dir(pg.LogFile), userName, userName, true)
		common.Chown(pg.MountPath, userName, userName, true)

		out := shell.RunAsUser(userName, cmdName, initDbArgs...)
		if !out.SucceededOrLog() {
			log.Fatal("Failed to run initdb for postgres")
		}

	} else {
		out := shell.Run(cmdName, initDbArgs...)
		if !out.SucceededOrLog() {
			log.Fatal("Failed to run initdb for postgres")
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
		out := shell.RunAsUser(userName, "mv", pg.ConfFileLocation, pg.dataDir)
		if !out.SucceededOrLog() {
			log.Fatal("failed to move postgres config")
		}
	} else {
		out := shell.Run("mv", pg.ConfFileLocation, pg.dataDir)
		if !out.SucceededOrLog() {
			log.Fatal("failed to move config.")
		}
	}
	pg.copyConfFiles() // move conf files back to conf location
}

func (pg Postgres) copyConfFiles() {
	// move conf files back to conf location
	userName := viper.GetString("service_username")

	// Add trailing slash to handle dataDir being a symlink
	findArgs := []string{pg.dataDir + "/", "-iname", "*.conf", "-exec", "cp", "{}",
		pg.ConfFileLocation, "\\;"}
	if common.HasSudoAccess() {
		common.MkdirAllOrFail(pg.ConfFileLocation, 0700)
		common.Chown(pg.ConfFileLocation, userName, userName, false)
		if out := shell.RunAsUser(userName, "find", findArgs...); !out.SucceededOrLog() {
			log.Fatal("failed to move config fails")
		}
	} else {
		common.MkdirAllOrFail(pg.ConfFileLocation, 0775)
		if out := shell.Run("find", findArgs...); !out.SucceededOrLog() {
			log.Fatal("failed to move config fails")
		}
	}
}

func (pg Postgres) createYugawareDatabase() {
	cmd := pg.PgBin + "/createdb"
	args := []string{
		"-h", pg.MountPath,
		"-U", pg.getPgUserName(),
		"yugaware",
	}
	var out *shell.Output
	if common.HasSudoAccess() {
		out = shell.RunAsUser(viper.GetString("service_username"), cmd, args...)
	} else {
		out = shell.Run(cmd, args...)
	}
	if !out.Succeeded() {
		if strings.Contains(out.StderrString(), "already exists") {
			// db already existing is fine because this may be a resumed failed install
			return
		}
		log.Fatal(fmt.Sprintf("Could not create yugaware database: %s", out.Error.Error()))
	}
}

// TODO: replace with pg_ctl status
// Status prints the status output specific to Postgres.
func (pg Postgres) Status() (common.Status, error) {
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
		return status, nil
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
		out := shell.Run("pgrep", "postgres")

		if out.Succeeded() {
			status.Status = common.StatusRunning
		} else if out.ExitCode == 1 {
			status.Status = common.StatusStopped
		} else {
			out.SucceededOrLog()
			return status, out.Error
		}
	}
	return status, nil
}

// CreateCronJob creates the cron job for managing postgres with cron script in non-root.
func (pg Postgres) CreateCronJob() {
	restartSeconds := viper.GetString("postgres.install.restartSeconds")

	shell.RunShell("(crontab", "-l", "2>/dev/null;", "echo", "\"@reboot", pg.cronScript,
		common.GetSoftwareRoot(), common.GetDataRoot(), restartSeconds, ")\"", "|",
		"sort", "-", "|", "uniq", "-", "|", "crontab", "-")
}
