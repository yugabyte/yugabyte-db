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

	"github.com/yugabyte/yugabyte-db/managed/yba-installer/common"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/common/shell"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/config"
	log "github.com/yugabyte/yugabyte-db/managed/yba-installer/logging"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/systemd"
)

type platformDirectories struct {
	SystemdFileLocation string
	ConfFileLocation    string
	templateFileName    string
	DataDir             string
	cronScript          string
	PgBin               string
}

func newPlatDirectories() platformDirectories {
	return platformDirectories{
		SystemdFileLocation: common.SystemdDir + "/yb-platform.service",
		ConfFileLocation:    common.GetSoftwareRoot() + "/yb-platform/conf/yb-platform.conf",
		templateFileName:    "yba-installer-platform.yml",
		DataDir:             common.GetBaseInstall() + "/data/yb-platform",
		cronScript: filepath.Join(
			common.GetInstallerSoftwareDir(), common.CronDir, "managePlatform.sh"),
		PgBin: common.GetSoftwareRoot() + "/pgsql/bin",
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
		platformDirectories: newPlatDirectories(),
		FixPaths:            false,
	}
}

func (plat Platform) devopsDir() string {
	return plat.yugabyteDir() + "/devops"
}

// yugaware dir has actual yugaware binary and JARs
func (plat Platform) yugawareDir() string {
	return plat.yugabyteDir() + "/yugaware"
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

// Install YBA service.
func (plat Platform) Install() error {
	log.Info("Starting Platform install")
	config.GenerateTemplate(plat)
	plat.createNecessaryDirectories()
	plat.untarDevopsAndYugawarePackages()
	plat.copyYugabyteReleaseFile()
	plat.copyYbcPackages()
	plat.copyNodeAgentPackages()
	plat.renameAndCreateSymlinks()
	err := createPemFormatKeyAndCert()
	if err != nil {
		return err
	}

	//Create the platform.log file so that we can start platform as
	//a background process for non-root.
	common.Create(common.GetSoftwareRoot() + "/yb-platform/yugaware/bin/platform.log")

	//Crontab based monitoring for non-root installs.
	if !common.HasSudoAccess() {
		plat.CreateCronJob()
	} else {
		// Allow yugabyte user to fully manage this installation (GetBaseInstall() to be safe)
		userName := viper.GetString("service_username")
		common.Chown(common.GetBaseInstall(), userName, userName, true)
	}

	plat.Start()
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

func (plat Platform) untarDevopsAndYugawarePackages() {

	log.Info("Extracting devops and yugaware packages.")

	packageFolderPath := plat.yugabyteDir()

	files, err := os.ReadDir(packageFolderPath)
	if err != nil {
		log.Fatal("Error: " + err.Error() + ".")
	}

	for _, f := range files {
		if strings.Contains(f.Name(), "devops") && strings.Contains(f.Name(), "tar") {

			devopsTgzName := f.Name()
			devopsTgzPath := packageFolderPath + "/" + devopsTgzName
			rExtract, errExtract := os.Open(devopsTgzPath)
			if errExtract != nil {
				log.Fatal("Error in starting the File Extraction process.")
			}

			log.Debug("Extracting archive at " + devopsTgzPath)
			if err := tar.Untar(rExtract, packageFolderPath+"/devops",
				tar.WithMaxUntarSize(-1)); err != nil {
				log.Fatal(fmt.Sprintf("failed to extract file %s, error: %s", devopsTgzPath, err.Error()))
			}
			log.Debug("Completed extracting archive at " + devopsTgzPath +
				" -> " + packageFolderPath + "/devops")

		} else if strings.Contains(f.Name(), "yugaware") && strings.Contains(f.Name(), "tar") {

			yugawareTgzName := f.Name()
			yugawareTgzPath := packageFolderPath + "/" + yugawareTgzName
			rExtract, errExtract := os.Open(yugawareTgzPath)
			if errExtract != nil {
				log.Fatal("Error in starting the File Extraction process.")
			}

			log.Debug("Extracting archive at " + yugawareTgzPath)
			if err := tar.Untar(rExtract, packageFolderPath+"/yugaware",
				tar.WithMaxUntarSize(-1)); err != nil {
				log.Fatal(fmt.Sprintf("failed to extract file %s, error: %s", yugawareTgzPath, err.Error()))
			}
			log.Debug("Completed extracting archive at " + yugawareTgzPath +
				" -> " + packageFolderPath + "/yugaware")

		}
	}

}

func (plat Platform) copyYugabyteReleaseFile() {

	packageFolderPath := plat.yugabyteDir()

	files, err := os.ReadDir(packageFolderPath)
	if err != nil {
		log.Fatal("Error: " + err.Error() + ".")
	}

	for _, f := range files {
		if strings.Contains(f.Name(), "yugabyte") {

			yugabyteTgzName := f.Name()
			yugabyteTgzPath := packageFolderPath + "/" + yugabyteTgzName
			common.CopyFile(yugabyteTgzPath,
				common.GetBaseInstall()+"/data/yb-platform/releases/"+plat.version+"/"+yugabyteTgzName)

		}
	}
}

func (plat Platform) copyYbcPackages() {
	packageFolderPath := common.GetInstallerSoftwareDir() + "/packages/yugabyte-" + plat.version
	ybcPattern := packageFolderPath + "/**/ybc/ybc*.tar.gz"

	matches, err := filepath.Glob(ybcPattern)
	if err != nil {
		log.Fatal(
			fmt.Sprintf("Could not find ybc components in %s. Failed with err %s",
				packageFolderPath, err.Error()))
	}

	for _, f := range matches {
		_, fileName := filepath.Split(f)
		// TODO: Check if file does not already exist?
		common.CopyFile(f, common.GetBaseInstall()+"/data/yb-platform/ybc/release/"+fileName)
	}

}

func (plat Platform) deleteNodeAgentPackages() {
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
}

func (plat Platform) copyNodeAgentPackages() {
	// Node-agent package is under yugabundle folder.
	packageFolderPath := common.GetInstallerSoftwareDir() + "/packages/yugabyte-" + plat.version
	nodeAgentPattern := packageFolderPath + "/node_agent-*.tar.gz"

	matches, err := filepath.Glob(nodeAgentPattern)
	if err != nil {
		log.Fatal(
			fmt.Sprintf("Could not find node-agent components in %s. Failed with err %s",
				packageFolderPath, err.Error()))
	}

	for _, f := range matches {
		_, fileName := filepath.Split(f)
		common.CopyFile(f, common.GetBaseInstall()+"/data/yb-platform/node-agent/releases/"+fileName)
	}

}

func (plat Platform) renameAndCreateSymlinks() {

	common.CreateSymlink(plat.yugabyteDir(), common.GetSoftwareRoot()+"/yb-platform", "yugaware")
	common.CreateSymlink(plat.yugabyteDir(), common.GetSoftwareRoot()+"/yb-platform", "devops")

}

// Start the YBA platform service.
func (plat Platform) Start() error {
	if common.HasSudoAccess() {
		if out := shell.Run(common.Systemctl, "daemon-reload"); !out.SucceededOrLog() {
			return out.Error
		}
		if out := shell.Run(common.Systemctl, "enable",
			filepath.Base(plat.SystemdFileLocation)); !out.SucceededOrLog() {
			return out.Error
		}
		if out := shell.Run(common.Systemctl, "start",
			filepath.Base(plat.SystemdFileLocation)); !out.SucceededOrLog() {
			return out.Error
		}
	} else {
		containerExposedPort := config.GetYamlPathData("platform.port")
		restartSeconds := config.GetYamlPathData("platform.restartSeconds")

		arg1 := []string{common.GetSoftwareRoot(), common.GetDataRoot(), containerExposedPort,
			restartSeconds, " > /dev/null 2>&1 &"}
		if out := shell.RunShell(plat.cronScript, arg1...); !out.SucceededOrLog() {
			return out.Error
		}
	}
	return nil
}

// Stop the YBA platform service.
func (plat Platform) Stop() error {
	status, err := plat.Status()
	if err != nil {
		return err
	}
	if status.Status != common.StatusRunning {
		log.Debug(plat.name + " is already stopped")
		return nil
	}
	if common.HasSudoAccess() {

		if out := shell.Run(common.Systemctl, "stop",
			filepath.Base(plat.SystemdFileLocation)); !out.SucceededOrLog() {
			return out.Error
		}
	} else {

		// Delete the file used by the crontab bash script for monitoring.
		common.RemoveAll(common.GetSoftwareRoot() + "/yb-platform/testfile")

		out := shell.Run("pgrep", "-fl", "yb-platform")
		if !out.SucceededOrLog() {
			return out.Error
		}
		result := out.StdoutString()
		// Need to stop the binary if it is running, can just do kill -9 PID (will work as the
		// process itself was started by a non-root user.)

		// Java check because pgrep will count the execution of yba-ctl as a process itself.
		if strings.TrimSuffix(string(result), "\n") != "" {
			pids := strings.Split(string(result), "\n")
			for _, pid := range pids {
				if strings.Contains(pid, "java") {
					log.Debug("kill platform pid: " + pid)
					if out := shell.Run("kill", "-9", pid); !out.SucceededOrLog() {
						return out.Error
					}
				}
			}
		}
	}
	return nil
}

// Restart the YBA platform service.
func (plat Platform) Restart() error {
	log.Info("Restarting YBA..")

	if common.HasSudoAccess() {
		out := shell.Run(common.Systemctl, "restart", "yb-platform.service")
		if !out.SucceededOrLog() {
			return out.Error
		}
	} else {
		if err := plat.Stop(); err != nil {
			return err
		}
		if err := plat.Start(); err != nil {
			return err
		}
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
	if common.HasSudoAccess() {
		err := os.Remove(plat.SystemdFileLocation)
		if err != nil {
			pe := err.(*fs.PathError)
			if !errors.Is(pe.Err, fs.ErrNotExist) {
				log.Info(fmt.Sprintf("Error %s removing systemd service %s.",
					pe.Error(), plat.SystemdFileLocation))
			}
		}
		// reload systemd daemon
		if out := shell.Run(common.Systemctl, "daemon-reload"); !out.SucceededOrLog() {
			return out.Error
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
	if common.HasSudoAccess() {
		status.ServiceFileLoc = plat.SystemdFileLocation
	} else {
		status.ServiceFileLoc = "N/A"
	}

	// Get the service status
	if common.HasSudoAccess() {
		props := systemd.Show(filepath.Base(plat.SystemdFileLocation), "LoadState", "SubState",
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
		out := shell.Run("pgrep", "-f", "yb-platform")
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

// Upgrade will upgrade the platform and install it into the alt install directory.
// Upgrade will NOT restart the service, the old version is expected to still be running
func (plat Platform) Upgrade() error {
	plat.platformDirectories = newPlatDirectories()
	config.GenerateTemplate(plat) // systemctl reload is not needed, start handles it for us.
	plat.createNecessaryDirectories()
	plat.untarDevopsAndYugawarePackages()
	plat.copyYugabyteReleaseFile()
	plat.copyYbcPackages()
	plat.deleteNodeAgentPackages()
	plat.copyNodeAgentPackages()
	plat.renameAndCreateSymlinks()
	pemErr := createPemFormatKeyAndCert()
	if pemErr != nil {
		return pemErr
	}
	//Create the platform.log file so that we can start platform as
	//a background process for non-root.
	common.Create(common.GetSoftwareRoot() + "/yb-platform/yugaware/bin/platform.log")

	//Crontab based monitoring for non-root installs.
	if !common.HasSudoAccess() {
		plat.CreateCronJob()
	} else {
		// Allow yugabyte user to fully manage this installation (GetBaseInstall() to be safe)
		userName := viper.GetString("service_username")
		common.Chown(common.GetBaseInstall(), userName, userName, true)
	}
	err := plat.Start()
	return err
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

// CreateCronJob creates the cron job for managing YBA platform with cron script in non-root.
func (plat Platform) CreateCronJob() {
	containerExposedPort := config.GetYamlPathData("platform.port")
	restartSeconds := config.GetYamlPathData("platform.restartSeconds")
	shell.RunShell("(crontab", "-l", "2>/dev/null;", "echo", "\"@reboot", plat.cronScript,
		common.GetSoftwareRoot(), common.GetDataRoot(), containerExposedPort, restartSeconds, ")\"", "|",
		"sort", "-", "|", "uniq", "-", "|", "crontab", "-")
}
