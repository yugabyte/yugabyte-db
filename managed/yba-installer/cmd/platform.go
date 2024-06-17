/*
 * Copyright (c) YugaByte, Inc.
 */

package cmd

import (
	"errors"
	"fmt"
	"io"
	"io/fs"
	"os"
	"path/filepath"
	"strings"

	"github.com/fluxcd/pkg/tar"
	"github.com/spf13/viper"

	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/common"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/common/shell"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/config"
	log "github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/logging"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/replicated/replicatedctl"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/systemd"
)

type platformDirectories struct {
	SystemdFileLocation string
	ConfFileLocation    string
	templateFileName    string
	DataDir             string
	PgBin               string
	YsqlDump            string
	YsqlBin             string
	PlatformPackages    string
}

func newPlatDirectories(version string) platformDirectories {
	return platformDirectories{
		SystemdFileLocation: common.SystemdDir + "/yb-platform.service",
		ConfFileLocation:    common.GetSoftwareRoot() + "/yb-platform/conf/yb-platform.conf",
		templateFileName:    "yba-installer-platform.yml",
		DataDir:             common.GetBaseInstall() + "/data/yb-platform",
		PgBin:               common.GetSoftwareRoot() + "/pgsql/bin",
		YsqlDump:            common.GetActiveSymlink() + "/ybdb/postgres/bin/ysql_dump",
		YsqlBin:             common.GetSoftwareRoot() + "/ybdb/bin/ysqlsh",
		PlatformPackages:    common.GetInstallerSoftwareDir() + "/packages/yugabyte-" + version,
	}
}

// Component 3: Platform
type Platform struct {
	name     string
	version  string
	FixPaths bool
	platformDirectories
}

// NewPlatform creates a new YBA service struct.
func NewPlatform(version string) Platform {
	return Platform{
		name:                "yb-platform",
		version:             version,
		platformDirectories: newPlatDirectories(version),
		FixPaths:            false,
	}
}

func (plat Platform) devopsDir() string {
	return plat.PlatformPackages + "/devops"
}

// yugaware dir has actual yugaware binary and JARs
func (plat Platform) yugawareDir() string {
	return plat.PlatformPackages + "/yugaware"
}

func (plat Platform) packageFolder() string {
	return "yugabyte-" + plat.version
}

func (plat Platform) yugabyteDir() string {
	return common.GetInstallerSoftwareDir() + "/packages/" + plat.packageFolder()
}

func (plat Platform) backupScript() string {
	return plat.devopsDir() + "/bin/yb_platform_backup.sh"
}

// TemplateFile returns the templated config file path that is used to generate yb-platform.conf.
func (plat Platform) TemplateFile() string {
	return plat.templateFileName
}

// Name returns the name of the service.
func (plat Platform) Name() string {
	return plat.name
}

func (plat Platform) SystemdFile() string {
	return plat.SystemdFileLocation
}

// Version gets the version
func (plat Platform) Version() string {
	return plat.version
}

// Install YBA service.
func (plat Platform) Install() error {
	log.Info("Starting Platform install")
	config.GenerateTemplate(plat)

	if err := plat.createNecessaryDirectories(); err != nil {
		return err
	}

	if err := plat.untarDevopsAndYugawarePackages(); err != nil {
		return err
	}
	if err := plat.copyYbcPackages(); err != nil {
		return err
	}
	if err := plat.copyNodeAgentPackages(); err != nil {
		return err
	}
	if err := plat.renameAndCreateSymlinks(); err != nil {
		return err
	}
	if err := createPemFormatKeyAndCert(); err != nil {
		return err
	}

	//Create the platform.log file so that we can start platform as
	//a background process for non-root.
	logFile := common.GetSoftwareRoot() + "/yb-platform/yugaware/bin/platform.log"
	createClosure := func() error {
		_, err := common.Create(logFile)
		return err
	}
	if err := createClosure(); err != nil {
		log.Error("Failed to create " + logFile + ": " + err.Error())
		return err
	}

	if common.HasSudoAccess() {
		// Allow yugabyte user to fully manage this installation (GetBaseInstall() to be safe)
		userName := viper.GetString("service_username")
		if err := changeAllPermissions(userName); err != nil {
			log.Error("Failed to set ownership of " + common.GetBaseInstall() + ": " + err.Error())
			return err
		}
	}

	if err := plat.Start(); err != nil {
		return err
	}
	log.Info("Finishing Platform install")
	return nil
}

// SetDataDirPerms sets the YBA data dir's permissions to the service username.
func (plat Platform) SetDataDirPerms() error {
	userName := viper.GetString("service_username")
	if err := common.Chown(plat.DataDir, userName, userName, true); err != nil {
		return err
	}
	return nil
}

func (plat Platform) createNecessaryDirectories() error {
	dirs := []string{
		common.GetSoftwareRoot() + "/yb-platform",
		common.GetBaseInstall() + "/data/yb-platform/releases",
		common.GetBaseInstall() + "/data/yb-platform/ybc/release",
		common.GetBaseInstall() + "/data/yb-platform/ybc/releases",
		common.GetBaseInstall() + "/data/yb-platform/node-agent/releases",
		plat.devopsDir(),
		plat.yugawareDir(),
	}
	userName := viper.GetString("service_username")
	for _, dir := range dirs {
		if _, err := os.Stat(dir); errors.Is(err, os.ErrNotExist) {
			if mkErr := common.MkdirAll(dir, common.DirMode); mkErr != nil {
				log.Error("failed to make " + dir + ": " + err.Error())
				return mkErr
			}
			if common.HasSudoAccess() {
				if chErr := common.Chown(dir, userName, userName, true); chErr != nil {
					log.Error("failed to set ownership of " + dir + ": " + chErr.Error())
					return chErr
				}
			}
		}
	}
	return nil
}

func (plat Platform) untarDevopsAndYugawarePackages() error {

	log.Info("Extracting devops and yugaware packages.")

	packageFolderPath := plat.yugabyteDir()

	files, err := os.ReadDir(packageFolderPath)
	if err != nil {
		return fmt.Errorf("unable to find packages to untar: %w", err)
	}

	for _, f := range files {
		if strings.Contains(f.Name(), "devops") && strings.Contains(f.Name(), "tar") {

			devopsTgzName := f.Name()
			devopsTgzPath := packageFolderPath + "/" + devopsTgzName
			rExtract, errExtract := os.Open(devopsTgzPath)
			if errExtract != nil {
				return fmt.Errorf("Error in starting the File Extraction process: %w", err)
			}

			log.Debug("Extracting archive at " + devopsTgzPath)
			if err := tar.Untar(rExtract, packageFolderPath+"/devops",
				tar.WithMaxUntarSize(-1)); err != nil {
				return fmt.Errorf("failed to extract file %s, error: %w", devopsTgzPath, err)
			}
			log.Debug("Completed extracting archive at " + devopsTgzPath +
				" -> " + packageFolderPath + "/devops")

		} else if strings.Contains(f.Name(), "yugaware") && strings.Contains(f.Name(), "tar") {

			yugawareTgzName := f.Name()
			yugawareTgzPath := packageFolderPath + "/" + yugawareTgzName
			rExtract, errExtract := os.Open(yugawareTgzPath)
			if errExtract != nil {
				return fmt.Errorf("Error in starting the File Extraction process: %w", err)
			}

			log.Debug("Extracting archive at " + yugawareTgzPath)
			if err := tar.Untar(rExtract, packageFolderPath+"/yugaware",
				tar.WithMaxUntarSize(-1)); err != nil {
				return fmt.Errorf("failed to extract file %s, error: %w", yugawareTgzPath, err)
			}
			log.Debug("Completed extracting archive at " + yugawareTgzPath +
				" -> " + packageFolderPath + "/yugaware")
		}
	}
	return nil
}

func (plat Platform) copyYbcPackages() error {
	log.Debug("Copying YBC Packages")
	ybcPattern := plat.PlatformPackages + "/**/ybc/ybc*.tar.gz"

	matches, err := filepath.Glob(ybcPattern)
	if err != nil {
		return fmt.Errorf("Could not find ybc components in %s. Failed with err %w",
			plat.PlatformPackages, err.Error())
	}

	for _, f := range matches {
		_, fileName := filepath.Split(f)
		dest := common.GetBaseInstall() + "/data/yb-platform/ybc/release/" + fileName
		if _, err := os.Stat(dest); errors.Is(err, os.ErrNotExist) {
			common.CopyFile(f, dest)
		}
	}
	return nil
}

func (plat Platform) deleteNodeAgentPackages() error {
	log.Debug("Deleting old node agent packages")
	// It deletes existing node-agent packages on upgrade.
	// Even if it fails, it is ok.
	releasesFolderPath := common.GetBaseInstall() + "/data/yb-platform/node-agent/releases"
	nodeAgentPattern := releasesFolderPath + "/node_agent-*.tar.gz"
	matches, err := filepath.Glob(nodeAgentPattern)
	if err == nil {
		for _, f := range matches {
			os.Remove(f)
		}
	}
	return nil
}

func (plat Platform) copyNodeAgentPackages() error {
	log.Debug("Copying node agent packages")
	// Node-agent package is under yugabundle folder.
	nodeAgentPattern := plat.PlatformPackages + "/node_agent-*.tar.gz"

	matches, err := filepath.Glob(nodeAgentPattern)
	if err != nil {
		log.Fatal(
			fmt.Sprintf("Could not find node-agent components in %s. Failed with err %s",
				plat.PlatformPackages, err.Error()))
	}

	for _, f := range matches {
		_, fileName := filepath.Split(f)
		dest := common.GetBaseInstall() + "/data/yb-platform/node-agent/releases/" + fileName
		if _, err := os.Stat(dest); errors.Is(err, os.ErrNotExist) {
			common.CopyFile(f, dest)
		}
	}
	return nil
}

func (plat Platform) renameAndCreateSymlinks() error {

	ybPlat := common.GetSoftwareRoot() + "/yb-platform"
	if err := common.CreateSymlink(plat.PlatformPackages, ybPlat, "yugaware"); err != nil {
		log.Error("failed to create soft link for yugaware directory")
		return err
	}
	if err := common.CreateSymlink(plat.PlatformPackages, ybPlat, "devops"); err != nil {
		log.Error("failed to create soft link for devops directory")
		return err
	}
	return nil
}

// Start the YBA platform service.
func (plat Platform) Start() error {
	serviceName := filepath.Base(plat.SystemdFileLocation)
	if err := systemd.DaemonReload(); err != nil {
		return fmt.Errorf("failed to start platform: %w", err)
	}
	if err := systemd.Enable(false, serviceName); err != nil {
		return fmt.Errorf("failed to start platform: %w", err)
	}
	if err := systemd.Start(serviceName); err != nil {
		return fmt.Errorf("failed to start platform: %w", err)
	}
	log.Debug("started platform")
	return nil
}

// Stop the YBA platform service.
func (plat Platform) Stop() error {
	serviceName := filepath.Base(plat.SystemdFileLocation)
	status, err := plat.Status()
	if err != nil {
		return err
	}
	if status.Status != common.StatusRunning {
		log.Debug(plat.name + " is already stopped")
		return nil
	}
	if err := systemd.Stop(serviceName); err != nil {
		return fmt.Errorf("failed to stop platform: %w", err)
	}
	log.Info("stopped platform")
	return nil
}

// Restart the YBA platform service.
func (plat Platform) Restart() error {
	serviceName := filepath.Base(plat.SystemdFileLocation)
	log.Info("Restarting YBA..")
	if err := systemd.DaemonReload(); err != nil {
		return fmt.Errorf("failed to restart platform: %w", err)
	}
	if err := systemd.Restart(serviceName); err != nil {
		return fmt.Errorf("failed to restart platform: %w", err)
	}
	return nil
}

// Uninstall the YBA platform service and optionally clean out data.
func (plat Platform) Uninstall(removeData bool) error {
	log.Info("Uninstalling yb-platform")

	// Stop running platform service
	if err := plat.Stop(); err != nil {
		return err
	}

	// Clean up systemd file
	err := os.Remove(plat.SystemdFileLocation)
	if err != nil {
		pe := err.(*fs.PathError)
		if !errors.Is(pe.Err, fs.ErrNotExist) {
			log.Info(fmt.Sprintf("Error %s removing systemd service %s.",
				pe.Error(), plat.SystemdFileLocation))
		}
		// reload systemd daemon
		if err := systemd.DaemonReload(); err != nil {
			return fmt.Errorf("failed to uninstall platform: %w")
		}
	}

	// Optionally remove data
	if removeData {
		err := common.RemoveAll(plat.DataDir)
		if err != nil {
			log.Info(fmt.Sprintf("Error %s removing data dir %s.", err.Error(), plat.DataDir))
		}
	}
	return nil
}

// Status prints the status output specific to yb-platform.
func (plat Platform) Status() (common.Status, error) {
	status := common.Status{
		Service:    plat.Name(),
		Port:       viper.GetInt("platform.port"),
		Version:    plat.version,
		ConfigLoc:  plat.ConfFileLocation,
		LogFileLoc: common.GetBaseInstall() + "/data/logs/application.log",
	}

	// Set the systemd service file location if one exists
	status.ServiceFileLoc = plat.SystemdFileLocation

	// Get the service status
	props := systemd.Show(filepath.Base(plat.SystemdFileLocation), "LoadState", "SubState",
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

// Upgrade will upgrade the platform and install it into the alt install directory.
// Upgrade will NOT restart the service, the old version is expected to still be running
func (plat Platform) Upgrade() error {
	plat.platformDirectories = newPlatDirectories(plat.version)
	if err := config.GenerateTemplate(plat); err != nil {
		return err
	} // systemctl reload is not needed, start handles it for us.
	if err := plat.createNecessaryDirectories(); err != nil {
		return err
	}
	if err := plat.untarDevopsAndYugawarePackages(); err != nil {
		return err
	}
	if err := plat.copyYbcPackages(); err != nil {
		return err
	}
	if err := plat.deleteNodeAgentPackages(); err != nil {
		return err
	}
	if err := plat.copyNodeAgentPackages(); err != nil {
		return err
	}
	if err := plat.renameAndCreateSymlinks(); err != nil {
		return err
	}
	if err := createPemFormatKeyAndCert(); err != nil {
		return err
	}
	//Create the platform.log file so that we can start platform as
	//a background process for non-root.
	logfile := common.GetSoftwareRoot() + "/yb-platform/yugaware/bin/platform.log"
	if _, err := common.Create(logfile); err != nil {
		log.Error("Failed to create " + logfile + ": " + err.Error())
		return err
	}

	if common.HasSudoAccess() {
		// Allow yugabyte user to fully manage this installation (GetBaseInstall() to be safe)
		userName := viper.GetString("service_username")
		if err := changeAllPermissions(userName); err != nil {
			log.Error("Failed to set ownership of " + common.GetBaseInstall() + ": " + err.Error())
			return err
		}
	}
	err := plat.Start()
	return err
}

// Helper function to update the data/software directories ownership to yugabyte user
func changeAllPermissions(user string) error {
	if err := common.Chown(common.GetBaseInstall()+"/data", user, user, true); err != nil {
		return err
	}

	if err := common.Chown(common.GetBaseInstall()+"/software", user, user, true); err != nil {
		return err
	}
	return nil
}

func (plat Platform) MigrateFromReplicated() error {
	config.GenerateTemplate(plat)

	if err := plat.createNecessaryDirectories(); err != nil {
		return err
	}
	if err := plat.untarDevopsAndYugawarePackages(); err != nil {
		return err
	}
	if err := plat.copyYbcPackages(); err != nil {
		return err
	}
	if err := plat.copyNodeAgentPackages(); err != nil {
		return err
	}

	if err := plat.symlinkReplicatedData(); err != nil {
		return fmt.Errorf("failed to migrated releases directory: %w", err)
	}
	if err := plat.renameAndCreateSymlinks(); err != nil {
		return err
	}

	pemVal, err := plat.pemFromDocker()
	if err != nil {
		log.Debug("no cert found from replicated, creating self signed")
		if err := createPemFormatKeyAndCert(); err != nil {
			return err
		}
	} else {
		log.Debug("found server.pem from docker.")
		serverPemPath := filepath.Join(common.GetSelfSignedCertsDir(), common.ServerPemPath)
		pemFile, err := common.Create(serverPemPath)
		if err != nil {
			log.Error(fmt.Sprintf("Failed to open server.pem with error: %s", err))
			return err
		}
		defer pemFile.Close()
		if _, err := pemFile.WriteString(pemVal); err != nil {
			return fmt.Errorf("failed to write pem file at %s: %w", serverPemPath, err)
		}

		log.Debug("Copying replicated ca key and cert for server.pem")
		// Key file
		common.CopyFile(filepath.Join(replicatedctl.SecretsDirectory, "ca.key"),
			filepath.Join(common.GetSelfSignedCertsDir(), "replicated-ca.key"))
		// Cert file
		common.CopyFile(filepath.Join(replicatedctl.SecretsDirectory, "ca.crt"),
			filepath.Join(common.GetSelfSignedCertsDir(), "replicated-ca.crt"))

	}

	//Create the platform.log file so that we can start platform as
	//a background process for non-root.
	logFile := common.GetSoftwareRoot() + "/yb-platform/yugaware/bin/platform.log"
	createClosure := func() error {
		_, err := common.Create(logFile)
		return err
	}
	if err := createClosure(); err != nil {
		log.Error("Failed to create " + logFile + ": " + err.Error())
		return err
	}

	if common.HasSudoAccess() {
		// Allow yugabyte user to fully manage this installation (GetBaseInstall() to be safe)
		userName := viper.GetString("service_username")
		if err := changeAllPermissions(userName); err != nil {
			log.Error("Failed to set ownership of " + common.GetBaseInstall() + ": " + err.Error())
			return err
		}
	}

	log.Info("Finishing Platform migration")
	return nil
}

// FinishReplicatedMigrate completest the replicated migration platform specific tasks
func (plat Platform) FinishReplicatedMigrate() error {
	files, err := os.ReadDir(filepath.Join(common.GetBaseInstall(), "data/yb-platform/releases"))
	if err != nil {
		return fmt.Errorf("could not read releases directory: %w", err)
	}
	for _, file := range files {
		if file.Type() != fs.ModeSymlink {
			log.DebugLF("skipping directory " + file.Name() + " as it is not a symlink")
			continue
		}
		src := filepath.Join(common.GetReplicatedBaseDir(), "releases", file.Name())
		target := filepath.Join(common.GetBaseInstall(), "data/yb-platform/releases", file.Name())
		err = common.ResolveSymlink(src, target)
		if err != nil {
			return fmt.Errorf("could not complete migration of platform: %w", err)
		}
	}
	return nil
}

func createPemFormatKeyAndCert() error {
	keyFile := viper.GetString("server_key_path")
	certFile := viper.GetString("server_cert_path")
	log.Info(fmt.Sprintf("Generating concatenated PEM from %s %s ", keyFile, certFile))

	// Open and read the key file.
	keyIn, err := os.Open(keyFile)
	if err != nil {
		log.Error(fmt.Sprintf("Failed to open server.key for reading with error: %s", err))
		return err
	}
	defer keyIn.Close()

	// Open and read the cert file.
	certIn, err := os.Open(certFile)
	if err != nil {
		log.Error(fmt.Sprintf("Failed to open server.cert for reading with error: %s", err))
		return err
	}
	defer certIn.Close()

	// Create this new concatenated PEM file to write key and cert in order.
	serverPemPath := filepath.Join(common.GetSelfSignedCertsDir(), common.ServerPemPath)
	pemFile, err := common.Create(serverPemPath)
	if err != nil {
		log.Error(fmt.Sprintf("Failed to open server.pem with error: %s", err))
		return err
	}
	defer pemFile.Close()

	// Append the key file into server.pem file.
	n, err := io.Copy(pemFile, keyIn)
	if err != nil {
		log.Error(fmt.Sprintf("Failed to append server.key to server.pem with error: %s", err))
		return err
	}
	log.Debug(fmt.Sprintf("Wrote %d bytes of %s to %s\n", n, keyFile, serverPemPath))
	// Append the cert file into server.pem file.
	n1, err := io.Copy(pemFile, certIn)
	if err != nil {
		log.Error(fmt.Sprintf("Failed to append server.cert to server.pem with error: %s", err))
		return err
	}
	log.Debug(fmt.Sprintf("Wrote %d bytes of %s to %s\n", n1, certFile, serverPemPath))

	if common.HasSudoAccess() {
		userName := viper.GetString("service_username")
		common.Chown(common.GetSelfSignedCertsDir(), userName, userName, true)
	}
	return nil
}

func (plat Platform) symlinkReplicatedData() error {
	// First do the previous releases.
	releases, err := os.ReadDir(filepath.Join(common.GetReplicatedBaseDir(), "releases/"))
	if err != nil {
		return fmt.Errorf("could not read replicated releases dir: %w", err)
	}
	for _, release := range releases {
		src := filepath.Join(common.GetReplicatedBaseDir(), "releases", release.Name())
		dest := filepath.Join(common.GetBaseInstall(), "data/yb-platform/releases", release.Name())
		err = common.Symlink(src, dest)
		if err != nil {
			return fmt.Errorf("failed symlinked release %s: %w", release.Name(), err)
		}
	}
	return nil
}

// pemFromDocker will read the pem file for https from server.pem. This only works for 2.16 and up.
func (plat Platform) pemFromDocker() (string, error) {
	out := shell.Run("docker", "exec", "yugaware", "cat",
		"/opt/yugabyte/yugaware/conf/server.pem")
	if !out.Succeeded() {
		out.LogDebug()
		return "", fmt.Errorf("failed to get pem file from container: %w", out.Error)
	}
	return out.StdoutString(), nil
}
