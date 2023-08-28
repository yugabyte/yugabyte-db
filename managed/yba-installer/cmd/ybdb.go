package cmd

import (
	"errors"
	"fmt"
	"io/fs"
	"os"
	"path/filepath"
	"strings"
	"time"

	"github.com/spf13/viper"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/common"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/common/shell"
	log "github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/logging"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/systemd"
)

type ybdbDirectories struct {
	SystemdFileLocation string
	ConfFileLocation    string
	templateFileName    string
	BaseDir             string
	dataDir             string
	YbdBin              string
	ybdbInstallDir      string
	ysqlBin             string
	LogDir              string
	LogFile             string
	cronScript          string
}

func newYbdbDirectories() ybdbDirectories {
	return ybdbDirectories{
		SystemdFileLocation: common.SystemdDir + "/ybdb.service",

		ConfFileLocation: common.GetSoftwareRoot() + "/ybdb/conf",
		// TODO: fix this (conf shd be in data dir or in its own dir)

		templateFileName: "yba-installer-ybdb.yml",
		BaseDir:          common.GetBaseInstall() + "/data/ybdb",
		dataDir:          common.GetBaseInstall() + "/data/ybdb/data",
		YbdBin:           common.GetSoftwareRoot() + "/ybdb/bin/yugabyted",
		ysqlBin:          common.GetSoftwareRoot() + "/ybdb/bin/ysqlsh",
		ybdbInstallDir:   common.GetSoftwareRoot() + "/ybdb",
		LogDir:           common.GetBaseInstall() + "/data/ybdb/logs",
		LogFile:          common.GetBaseInstall() + "/data/ybdb/logs/yugabyted.log",
		cronScript: filepath.Join(
			common.GetInstallerSoftwareDir(), common.CronDir, "manageYbdb.sh")}
}

// YBDB component
type Ybdb struct {
	name    string
	version string
	ybdbDirectories
}

// NewYbdb creates a new ybdb service struct at installRoot with specific version.
func NewYbdb(version string) Ybdb {
	return Ybdb{
		name:            "ybdb",
		version:         version,
		ybdbDirectories: newYbdbDirectories(),
	}
}

// TemplateFile returns YBDB's templated config file path
func (ybdb Ybdb) TemplateFile() string {
	return ybdb.templateFileName
}

// YBDB service name
func (ybdb Ybdb) Name() string {
	return ybdb.name
}

// Returns status of YBDB service.
func (ybdb Ybdb) Status() (common.Status, error) {
	status := common.Status{
		Service: ybdb.name,
		Port:    viper.GetInt("ybdb.install.port"),
		Version: ybdb.version,
	}

	status.ConfigLoc = ybdb.ConfFileLocation
	status.LogFileLoc = ybdb.LogFile

	if common.HasSudoAccess() {
		status.ServiceFileLoc = ybdb.SystemdFileLocation
	} else {
		//TODO: Handle non-sudo scenario
		status.ServiceFileLoc = "N/A"
	}

	if common.HasSudoAccess() {
		//TODO: see if this common method can be moved to systemd.
		props := systemd.Show(filepath.Base(ybdb.SystemdFileLocation), "LoadState", "SubState",
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
		// TODO: Handle non-sudo scenario
	}
	return status, nil
}

// Stops YBDB service
func (ybdb Ybdb) Stop() error {
	status, err := ybdb.Status()
	if err != nil {
		return err
	}

	if status.Status != common.StatusRunning {
		log.Debug(ybdb.name + " is already stopped")
		return nil
	}

	if common.HasSudoAccess() {
		out := shell.Run(common.Systemctl, "stop", filepath.Base(ybdb.SystemdFileLocation))
		if !out.SucceededOrLog() {
			return out.Error
		}
	} else {
		//TODO: Handle non-sudo case
		log.Fatal("YBDB cannot be stopped in non-sudo mode.")
	}
	return nil
}

// Uninstalls YBDB service (Also removes data).
func (ybdb Ybdb) Uninstall(removeData bool) error {
	log.Info("Uninstalling ybdb")
	if err := ybdb.Stop(); err != nil {
		return err
	}

	if removeData {
		// Remove ybdb data directory
		if err := common.RemoveAll(ybdb.BaseDir); err != nil {
			log.Info(fmt.Sprintf("Error %s removing ybdb data dir %s.", err.Error(), ybdb.dataDir))
			return err
		}

		if err := common.RemoveAll(ybdb.ybdbInstallDir); err != nil {
			log.Info(fmt.Sprintf("Error %s removing ybdb data dir %s.", err.Error(), ybdb.ybdbInstallDir))
			return err
		}
	}

	if common.HasSudoAccess() {
		err := os.Remove(ybdb.SystemdFileLocation)
		if err != nil {
			pe := err.(*fs.PathError)
			if !errors.Is(pe.Err, fs.ErrNotExist) {
				log.Info(fmt.Sprintf("Error %s removing systemd service %s.",
					err.Error(), ybdb.SystemdFileLocation))
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

// Starts YBDB service.
func (ybdb Ybdb) Start() error {
	if common.HasSudoAccess() {

		if out := shell.Run(common.Systemctl, "daemon-reload"); !out.SucceededOrLog() {
			return out.Error
		}

		if out := shell.Run(common.Systemctl, "enable",
			filepath.Base(ybdb.SystemdFileLocation)); !out.SucceededOrLog() {
			return out.Error
		}

		if out := shell.Run(common.Systemctl, "start",
			filepath.Base(ybdb.SystemdFileLocation)); !out.SucceededOrLog() {
			return out.Error
		}

		if out := shell.Run(common.Systemctl, "status",
			filepath.Base(ybdb.SystemdFileLocation)); !out.SucceededOrLog() {
			return out.Error
		}

	} else {
		//TODO: Handle non-sudo case
		log.Fatal("YBDB cannot be started in non-sudo mode.")
	}
	return nil
}

// Restarts YBDB service.
func (ybdb Ybdb) Restart() error {
	log.Info("Restarting ybdb..")

	if common.HasSudoAccess() {
		if out := shell.Run(common.Systemctl, "restart",
			filepath.Base(ybdb.SystemdFileLocation)); !out.SucceededOrLog() {
			return out.Error
		}
	} else {
		//TODO: non-sudo case
		if err := ybdb.Stop(); err != nil {
			return err
		}
		if err := ybdb.Start(); err != nil {
			return err
		}
	}
	return nil
}

func (ybdb Ybdb) getYbdbUsername() string {
	return "yugabyte"
}

// TODO: Implement YBDB Upgrade
func (ybdb Ybdb) Upgrade() error {
	//TODO: Implement upgrade
	return nil
}

// Install ybdb and create the yugaware DB for YBA.
func (ybdb Ybdb) Install() error {
	//config.GenerateTemplate(ybdb)

	if err := ybdb.extractYbdbPackage(); err != nil {
		return err
	}
	if err := ybdb.Start(); err != nil {
		return err
	}
	ybdb.WaitForYbdbReadyOrFatal(5)

	if err := ybdb.createYugawareDatabase(); err != nil {
		return err
	}
	if !common.HasSudoAccess() {
		if err := ybdb.CreateCronJob(); err != nil {
			return err
		}
	}
	return nil
}

// Install ybdb and create the yugaware DB for YBA.
func (ybdb Ybdb) createYugawareDatabase() error {
	cmd := ybdb.ysqlBin
	args := []string{
		"-U", ybdb.getYbdbUsername(),
		"-h", "localhost",
		"-p", viper.GetString("ybdb.install.port"),
		"-c",
		"CREATE DATABASE yugaware WITH colocated = true;",
	}
	var out *shell.Output
	if common.HasSudoAccess() {
		out = shell.RunAsUser(viper.GetString("service_username"), cmd, args...)
	} else {
		out = shell.Run(cmd, args...)
	}
	if !out.Succeeded() {
		if strings.Contains(out.Error.Error(), "already exists") {
			// db already existing is fine because this may be a resumed failed install
			return nil
		}
		log.Fatal(fmt.Sprintf("Could not create yugaware database: %s", out.Error.Error()))
	}
	return nil
}

func (ybdb Ybdb) extractYbdbPackage() error {
	// Download stable YBDB version from download.yugabyte.com
	// when it doesn't come packaged with yba-installer.
	ybdbPackagePath := common.MaybeGetYbdbPackagePath()
	if ybdbPackagePath == "" {
		log.Info("Downloading YBDB package from download.yugabyte.com.")
		ybdbPackageDownloadURL := "https://s3.us-west-2.amazonaws.com/downloads.yugabyte.com/releases/" +
			"2.17.2.0/yugabyte-2.17.2.0-b216-linux-x86_64.tar.gz"
		var err error
		ybdbPackagePath, err = common.DownloadFileToTmp(ybdbPackageDownloadURL, "ybdb.tar.gz")
		if err != nil {
			log.Fatal(fmt.Sprintf("Could not download ybdb package: %s", err.Error()))
		}
	}

	common.MkdirAllOrFail(ybdb.ybdbInstallDir, os.ModePerm)
	userName := viper.GetString("service_username")
	shell.Run("tar", "-zxf", ybdbPackagePath, "-C", ybdb.ybdbInstallDir, "--strip-components", "1")
	common.Chown(ybdb.ybdbInstallDir, userName, userName, true)
	return nil
}

func (ybdb Ybdb) WaitForYbdbReadyOrFatal(retryCount int) {
	//Wait for ysql to come up.
	for retryCount > 0 {
		retryCount--
		if ybdb.checkYbdbStatus() {
			return
		}
		time.Sleep(1)
	}
	log.Fatal("Unable to check ybdb status.")
}

func (ybdb Ybdb) checkYbdbStatus() bool {
	status, err := ybdb.queryYsql("select version();")
	if err != nil {
		log.Debug("YBDB status check failed - " + status)
		return false
	}
	log.Debug("YBDB status check passed - " + status)
	return true

}

func (ybdb Ybdb) queryYsql(query string) (string, error) {
	cmd := ybdb.ysqlBin
	args := []string{
		"-U", ybdb.getYbdbUsername(),
		"-h", "localhost",
		"-p", viper.GetString("ybdb.install.port"),
		"-c",
		query,
	}
	var out *shell.Output
	if common.HasSudoAccess() {
		out = shell.RunAsUser(viper.GetString("service_username"), cmd, args...)
	} else {
		out = shell.Run(cmd, args...)
	}
	if !out.Succeeded() {
		log.Debug("YSQL query failed: " + out.StderrString())
		return out.StderrString(), out.Error
	}
	log.Debug("YSQL query succeeded.")
	return out.StdoutString(), nil

}

// TODO: Create Cron Job for non-sudo sceanrios.
func (ybdb Ybdb) CreateCronJob() error {
	// TODO: Handle non-sudo case
	log.Fatal("Cannot create cron job for YBDB.")
	return nil
}

func (ybdb Ybdb) CreateBackup(backupPath ...string) {
	ysql_dump := filepath.Join(common.GetActiveSymlink(), "/ybdb/postgres/bin/ysql_dump")
	var outFile string
	if len(backupPath) > 0 {
		outFile = backupPath[0]
	} else {
		outFile = filepath.Join(common.GetBaseInstall(), "data", "ybdb_backup")
	}
	ybdb.createBackup(ysql_dump, outFile)
}

func (ybdb Ybdb) CreateBackupUsingPgDump(pgDumpPath string, backupPath string) {
	ybdb.createBackup(pgDumpPath, backupPath)
}

func (ybdb Ybdb) createBackup(dumpPath string, outFile string) {
	log.Debug("starting ybdb backup")

	// Remove existing backup
	if _, err := os.Stat(outFile); !errors.Is(err, os.ErrNotExist) {
		os.Remove(outFile)
	}
	file, err := os.Create(outFile)
	if err != nil {
		log.Fatal("failed to open file " + outFile + ": " + err.Error())
	}
	defer file.Close()

	// We want the active install directory even during the upgrade workflow.

	args := []string{
		"-p", "5433",
		"-h", "localhost",
		"-U", ybdb.getYbdbUsername(),
		"-f", outFile,
		"--clean",
		"yugaware",
	}
	out := shell.Run(dumpPath, args...)
	if !out.SucceededOrLog() {
		log.Fatal("ybdb backup failed: " + out.Error.Error())
	}
	log.Debug("ybdb backup comlete")
}

func (ybdb Ybdb) RestoreBackup(backupFile string) {
	log.Debug("ybdb starting restore from backup")
	inFile := backupFile
	if _, err := os.Stat(inFile); errors.Is(err, os.ErrNotExist) {
		log.Fatal("backup file does not exist")
	}

	args := []string{
		"-d", "yugaware",
		"-f", inFile,
		"-h", "localhost",
		"-p", viper.GetString("ybdb.install.port"),
		"-U", viper.GetString("service_username"),
		"-q",
	}

	out := shell.Run(ybdb.ysqlBin, args...)
	if !out.SucceededOrLog() {
		log.Fatal("ybdb restore from backup failed: " + out.Error.Error())
	}
	log.Debug("ybdb restore from backup complete")
}
