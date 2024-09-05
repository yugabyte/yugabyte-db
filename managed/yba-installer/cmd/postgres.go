/*
 * Copyright (c) YugaByte, Inc.
 */

package cmd

import (
	"errors"
	"fmt"
	"io/fs"
	"os"
	"path"
	"strings"

	"github.com/spf13/viper"

	"path/filepath"

	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/common"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/common/shell"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/config"
	log "github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/logging"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/systemd"
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
}

func newPostgresDirectories() postgresDirectories {
	return postgresDirectories{
		SystemdFileLocation: common.SystemdDir + "/postgres.service",
		ConfFileLocation:    common.GetSoftwareRoot() + "/pgsql/conf",
		templateFileName:    "yba-installer-postgres.yml",
		MountPath:           common.GetBaseInstall() + "/data/pgsql/run/postgresql",
		dataDir:             common.GetBaseInstall() + "/data/postgres",
		PgBin:               common.GetSoftwareRoot() + "/pgsql/bin",
		LogFile:             common.GetBaseInstall() + "/data/logs/postgres.log",
	}
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

func (pg Postgres) SystemdFile() string {
	return pg.SystemdFileLocation
}

// Name returns the name of the service.
func (pg Postgres) Name() string {
	return pg.name
}

// Version gets the version.
func (pg Postgres) Version() string {
	return pg.version
}

// Version gets the version
func (pg Postgres) getPgUserName() string {
	return viper.GetString("postgres.install.username")
}

// Install postgres and create the yugaware DB for YBA.
func (pg Postgres) Install() error {
	config.GenerateTemplate(pg)
	if err := pg.extractPostgresPackage(); err != nil {
		return err
	}

	if err := pg.createFilesAndDirs(); err != nil {
		return err
	}

	// First let initdb create its config and data files in the software/pg../conf location
	if err := pg.runInitDB(); err != nil {
		return err
	}

	if err := pg.copyConfFiles(); err != nil {
		return err
	}

	// Finally update the conf file location to match this new data dir location
	if err := pg.modifyPostgresConf(); err != nil {
		return err
	}

	if err := pg.Start(); err != nil {
		return fmt.Errorf("postgres install could not start postgres: %w", err)
	}

	// work to set up LDAP
	if viper.GetBool("postgres.install.ldap_enabled") {
		if err := pg.setUpLDAP(); err != nil {
			return err
		}
	}

	// Set the password if we are doing the install
	if viper.GetBool("postgres.install.enabled") {
		pg.alterPassword()
	}

	if viper.GetBool("postgres.install.enabled") {
		pg.createYugawareDatabase()
	}
	return nil
}

// TODO: This should generate the correct start string based on installation mode
// and write it to the correct service file
// Start starts the postgres process either via systemd.
func (pg Postgres) Start() error {
	serviceName := filepath.Base(pg.SystemdFileLocation)
	if err := systemd.DaemonReload(); err != nil {
		return fmt.Errorf("failed to start postgres: %w", err)
	}
	if err := systemd.Enable(false, serviceName); err != nil {
		return fmt.Errorf("failed to start postgres: %w", err)
	}
	if err := systemd.Start(serviceName); err != nil {
		return fmt.Errorf("failed to start postgres: %w", err)
	}
	log.Debug("started postgres")
	return nil
}

// Stop stops the postgres process either via systemd.
func (pg Postgres) Stop() error {
	serviceName := filepath.Base(pg.SystemdFileLocation)
	status, err := pg.Status()
	if err != nil {
		return err
	}
	if status.Status != common.StatusRunning {
		log.Debug(pg.name + " is already stopped")
		return nil
	}
	if err := systemd.Stop(serviceName); err != nil {
		return fmt.Errorf("failed to stop postgres: %w", err)
	}
	log.Info("stopped postgres")
	return nil
}

func (pg Postgres) Restart() error {
	serviceName := filepath.Base(pg.SystemdFileLocation)
	log.Info("Restarting postgres..")
	if err := systemd.DaemonReload(); err != nil {
		return fmt.Errorf("failed to restart postgres: %w", err)
	}
	if err := systemd.Restart(serviceName); err != nil {
		return fmt.Errorf("failed to restart postgres: %w", err)
	}
	return nil
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
	status.BinaryLoc = pg.PgBin

	// Set the systemd service file location if one exists
	status.ServiceFileLoc = pg.SystemdFileLocation

	// Get the service status
	props := systemd.Show(filepath.Base(pg.SystemdFileLocation), "LoadState", "SubState",
		"ActiveState", "ActiveEnterTimestamp", "ActiveExitTimestamp")
	if props["LoadState"] == "not-found" {
		status.Status = common.StatusNotInstalled
	} else if props["SubState"] == "running" {
		status.Status = common.StatusRunning
		status.Since = common.StatusSince(props["ActiveEnterTimestamp"])
	} else if props["ActiveState"] == "inactive" {
		status.Status = common.StatusStopped
		status.Since = common.StatusSince(props["ActiveExitTimestamp"])
	} else {
		status.Status = common.StatusErrored
		status.Since = common.StatusSince(props["ActiveExitTimestamp"])
	}
	return status, nil
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

		if err := common.RemoveAll(pg.ConfFileLocation); err != nil {
			log.Info(fmt.Sprintf("Error %s removing postgres conf dir %s.", err.Error(),
				pg.ConfFileLocation))
			return err
		}
	}

	err := os.Remove(pg.SystemdFileLocation)
	if err != nil {
		pe := err.(*fs.PathError)
		if !errors.Is(pe.Err, fs.ErrNotExist) {
			log.Info(fmt.Sprintf("Error %s removing systemd service %s.",
				err.Error(), pg.SystemdFileLocation))
			return err
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

func (pg Postgres) CreateYugawareBackup(outFile string) {
	log.Debug("Starting postgres yugaware database backup.")
	pg_dump := filepath.Join(common.GetActiveSymlink(), "/pgsql/bin/pg_dump")
	args := []string{
		"-p", viper.GetString("postgres.install.port"),
		"-h", "localhost",
		"-U", pg.getPgUserName(),
		"-Fp",
		"-f", outFile,
		"yugaware",
	}
	out := shell.Run(pg_dump, args...)
	if !out.SucceededOrLog() {
		log.Fatal("Yugaware postgres backup failed: " + out.Error.Error())
	}
	log.Debug("yugware postgres backup complete")
}

func (pg Postgres) RestoreBackup(backupPath string) {
	log.Debug("postgres starting restore from backup")
	if _, err := os.Stat(backupPath); errors.Is(err, os.ErrNotExist) {
		log.Fatal("backup file does not exist")
	}

	psql := filepath.Join(pg.PgBin, "psql")
	args := []string{
		"-d", "yugaware",
		"-f", backupPath,
		"-h", "localhost",
		"-p", viper.GetString("postgres.install.port"),
		"-U", pg.getPgUserName(),
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
func (pg Postgres) UpgradeMajorVersion() error {
	log.Info("Starting Postgres major upgrade")
	pg.CreateBackup()
	pg.Stop()
	pg.postgresDirectories = newPostgresDirectories()
	config.GenerateTemplate(pg) // NOTE: This does not require systemd reload, start does it for us.
	if err := pg.extractPostgresPackage(); err != nil {
		return err
	}

	if err := pg.createFilesAndDirs(); err != nil {
		return err
	}

	if err := pg.runInitDB(); err != nil {
		return err
	}
	if err := pg.copyConfFiles(); err != nil {
		return err
	}
	if err := pg.modifyPostgresConf(); err != nil {
		return err
	}
	pg.Start()
	backupFile := filepath.Join(common.GetBaseInstall(), "data", "postgres_backup")
	pg.RestoreBackup(backupFile)

	log.Info("Completed Postgres major upgrade")
	return nil
}

// Upgrade will do a minor version upgrade of postgres
func (pg Postgres) Upgrade() error {
	log.Info("Starting Postgres upgrade")
	pg.postgresDirectories = newPostgresDirectories()
	if err := config.GenerateTemplate(pg); err != nil {
		return err
	}
	if err := pg.extractPostgresPackage(); err != nil {
		return err
	}

	if err := pg.copyConfFiles(); err != nil {
		return err
	}

	if err := pg.modifyPostgresConf(); err != nil {
		return err
	}
	return nil
}

// MigrateFromReplicated performs the steps needed to migrate from an existing replicated install
// to a full yba-ctl install. As this work will expect a backup of postgres to be taken and then
// restored onto the new postgres install, we currently will just do a full install.
// TODO: Implement the backup/restore of postgres.
func (pg Postgres) MigrateFromReplicated() error {
	config.GenerateTemplate(pg)
	if err := pg.extractPostgresPackage(); err != nil {
		return fmt.Errorf("Error extracting postgres package: %s", err.Error())
	}

	if err := pg.createFilesAndDirs(); err != nil {
		return fmt.Errorf("Error creating postgres files and directories: %s", err.Error())
	}

	// First let initdb create its config and data files in the software/pg../conf location
	if err := pg.runInitDB(); err != nil {
		return fmt.Errorf("Error running initdb: %s", err.Error())
	}

	// Copy Config files from initdb
	if err := pg.copyConfFiles(); err != nil {
		return fmt.Errorf("Error copying config files: %s", err.Error())
	}
	// Finally update the conf file location to match this new data dir location
	if err := pg.modifyPostgresConf(); err != nil {
		return fmt.Errorf("postgres replicated migration failed: %w", err)
	}

	log.Info("Finished postgres migration")
	return nil
}

// TODO: Error handling for this function (need to return errors from createYugawareDB and start)
func (pg Postgres) replicatedMigrateStep2() error {
	pg.Start()

	if viper.GetBool("postgres.install.enabled") {
		pg.createYugawareDatabase()
	}
	return nil
}

// FinishReplicatedMigrate is a no-op for postgres, as this was done via backup, restore.
func (pg Postgres) FinishReplicatedMigrate() error {
	return nil
}

func (pg Postgres) extractPostgresPackage() error {
	postgresPackagePath := common.GetPostgresPackagePath()
	out := shell.Run("tar", "-zxf", postgresPackagePath, "-C", common.GetSoftwareRoot())
	if !out.SucceededOrLog() {
		return out.Error
	}
	return nil
}

func (pg Postgres) runInitDB() error {
	if _, err := os.Stat(pg.dataDir); !errors.Is(err, os.ErrNotExist) {
		log.Debug(fmt.Sprintf("pg config %s already exists, skipping init db", pg.ConfFileLocation))
		return nil
	}
	cmdName := pg.PgBin + "/initdb"
	initDbArgs := []string{
		"-U",
		pg.getPgUserName(),
		"-D",
		pg.dataDir,
		"--locale=" + viper.GetString("postgres.install.locale"),
	}
	if common.HasSudoAccess() {
		// Need to give the yugabyte user ownership of the entire postgres
		// directory.
		userName := viper.GetString("service_username")
		out := shell.RunAsUser(userName, cmdName, initDbArgs...)
		if !out.SucceededOrLog() {
			log.Error("Failed to run initdb for postgres")
			return out.Error
		}
	} else {
		out := shell.Run(cmdName, initDbArgs...)
		if !out.SucceededOrLog() {
			log.Error("Failed to run initdb for postgres")
			return out.Error
		}
	}
	return nil
}

func (pg Postgres) alterPassword() error {
	// Reload hba conf
	passwordCmd := fmt.Sprintf("ALTER USER %s PASSWORD '%s';",
		viper.GetString("postgres.install.username"),
		viper.GetString("postgres.install.password"))
	psql := filepath.Join(pg.PgBin, "psql")
	args := []string{
		"-d", "postgres",
		"-h", "localhost",
		"-p", viper.GetString("postgres.install.port"),
		"-U", pg.getPgUserName(),
		"-c", passwordCmd,
	}
	out := shell.Run(psql, args...)
	if !out.SucceededOrLog() {
		return out.Error
	}
	return nil
}

// Set the data directory in postgresql.conf
// Also sets up LDAP if necessary
func (pg Postgres) modifyPostgresConf() error {
	// work to set data directory separate in postgresql.conf
	pgConfPath := filepath.Join(pg.ConfFileLocation, "postgresql.conf")
	conf, err := os.ReadFile(pgConfPath)
	if err != nil {
		return fmt.Errorf("Error opening %s: %s", pgConfPath, err.Error())
	}
	lines := strings.Split(string(conf), "\n")
	foundData := false
	dataLine := fmt.Sprintf("data_directory = '%s'\n", pg.dataDir)
	foundPort := false
	portLine := fmt.Sprintf("port = %d\n", viper.GetInt("postgres.install.port"))
	for i, line := range lines {
		if strings.HasPrefix(line, "data_directory =") {
			lines[i] = dataLine
			foundData = true
		} else if strings.HasPrefix(line, "port =") {
			lines[i] = portLine
			foundPort = true
		}
	}

	if !foundData {
		lines = append(lines, dataLine)
	}
	if !foundPort {
		lines = append(lines, portLine)
	}

	err = os.WriteFile(pgConfPath, []byte(strings.Join(lines, "\n")), 0600)
	if err != nil {
		return fmt.Errorf("error writing pg conf file to %s: %s", pgConfPath, err.Error())
	}
	return nil
}

func (pg Postgres) setUpLDAP() error {
	pgHbaConfPath := filepath.Join(pg.ConfFileLocation, "pg_hba.conf")
	hbaConf, err := os.OpenFile(pgHbaConfPath, os.O_APPEND|os.O_WRONLY, 0600)
	if err != nil {
		return fmt.Errorf("Error opening %s: %s", pgHbaConfPath, err.Error())
	}
	defer hbaConf.Close()
	ldapServer := viper.GetString("postgres.install.ldap_server")
	ldapPrefix := viper.GetString("postgres.install.ldap_prefix")
	ldapSuffix := viper.GetString("postgres.install.ldap_suffix")
	ldapPort := viper.GetInt("postgres.install.ldap_port")
	ldapTLS := viper.GetBool("postgres.install.secure_ldap")
	_, err = hbaConf.WriteString(
		fmt.Sprintf("host all all all ldap ldapserver=%s ldapprefix=\"%s\" "+
			"ldapsuffix=\"%s\" ldapport=%d ldaptls=%d",
			ldapServer, ldapPrefix, ldapSuffix, ldapPort, common.Bool2Int(ldapTLS)))
	if err != nil {
		return fmt.Errorf("Error writing ldap config to %s: %s", pgHbaConfPath, err.Error())
	}

	// Reload hba conf
	reloadCmd := "SELECT pg_reload_conf();"
	psql := filepath.Join(pg.PgBin, "psql")
	args := []string{
		"-d", "postgres",
		"-h", "localhost",
		"-p", viper.GetString("postgres.install.port"),
		"-U", pg.getPgUserName(),
		"-c", reloadCmd,
	}
	out := shell.Run(psql, args...)
	if !out.SucceededOrLog() {
		return out.Error
	}
	return nil
}

func (pg Postgres) copyConfFiles() error {
	// move conf files back to conf location
	userName := viper.GetString("service_username")

	// Clean the conf directory from previous installs before copying over again
	err := common.RemoveAll(pg.ConfFileLocation)
	if err != nil {
		return fmt.Errorf("Error cleaning out %s: %w", pg.ConfFileLocation, err)
	}

	// setup config file location
	if common.HasSudoAccess() {
		common.MkdirAllOrFail(pg.ConfFileLocation, 0700)
		if err := common.Chown(pg.ConfFileLocation, userName, userName, false); err != nil {
			return fmt.Errorf("failed to change ownership of %s: %w", pg.ConfFileLocation, err)
		}
	} else {
		common.MkdirAllOrFail(pg.ConfFileLocation, 0775)
	}

	entries, err := os.ReadDir(pg.dataDir)
	if err != nil {
		return fmt.Errorf("failed to read pg data directory %s: %w", pg.dataDir, err)
	}

	// Get the config files
	var confPaths []string
	for _, entry := range entries {
		if strings.HasSuffix(entry.Name(), ".conf") {
			confPaths = append(confPaths, path.Join(pg.dataDir, entry.Name()))
		}
	}
	// copy the config files
	for _, confPath := range confPaths {
		log.Debug(fmt.Sprintf("Copying pg config files %s -> %s", confPath, pg.ConfFileLocation))
		if err := common.Copy(confPath, pg.ConfFileLocation, false, true); err != nil {
			return fmt.Errorf("failed to copy pg config files %s -> %s: %w",
				confPath, pg.ConfFileLocation, err)
		}
	}
	return nil
}

func (pg Postgres) createYugawareDatabase() {
	cmd := pg.PgBin + "/createdb"
	args := []string{
		"-h", pg.MountPath,
		"-U", pg.getPgUserName(),
		"-p", viper.GetString("postgres.install.port"),
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

func (pg Postgres) createFilesAndDirs() error {
	f, err := common.Create(common.GetBaseInstall() + "/data/logs/postgres.log")
	if err != nil && !errors.Is(err, os.ErrExist) {
		log.Error("Failed to create postgres logfile: " + err.Error())
		return err
	}
	if f != nil {
		f.Close()
	}

	// Needed for socket acceptance in the non-root case.
	if err := common.MkdirAll(pg.MountPath, os.ModePerm); err != nil {
		log.Error("failed to create " + pg.MountPath + ": " + err.Error())
		return err
	}

	if common.HasSudoAccess() {
		// Need to give the yugabyte user ownership of the entire postgres
		// directory.
		userName := viper.GetString("service_username")

		confDir := filepath.Dir(pg.ConfFileLocation)
		if err := common.Chown(confDir, userName, userName, true); err != nil {
			return err
		}
		if err := common.Chown(filepath.Dir(pg.LogFile), userName, userName, true); err != nil {
			return err
		}
		if err := common.Chown(pg.MountPath, userName, userName, true); err != nil {
			return err
		}
	}
	return nil
}

func (pg Postgres) symlinkReplicatedDir() error {
	repliData := "/opt/yugabyte/postgresql/14/yugaware"

	if err := common.Symlink(repliData, pg.dataDir); err != nil {
		return fmt.Errorf("failed to symlink postgres data: %w", err)
	}
	userName := viper.GetString("service_username")
	if err := common.Chown(pg.dataDir+"/", userName, userName, true); err != nil {
		return fmt.Errorf("failed to change ownership of postgres linked data to %s: %w", userName, err)
	}
	return nil
}
