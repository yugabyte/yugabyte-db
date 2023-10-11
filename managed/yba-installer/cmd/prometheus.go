/*
 * Copyright (c) YugaByte, Inc.
 */

package cmd

import (
	"errors"
	"fmt"
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

type prometheusDirectories struct {
	SystemdFileLocation string
	ConfFileLocation    string // This is used during config generation
	templateFileName    string
	DataDir             string
	PromDir             string
	cronScript          string
}

func newPrometheusDirectories() prometheusDirectories {
	return prometheusDirectories{
		SystemdFileLocation: common.SystemdDir + "/prometheus.service",
		ConfFileLocation:    common.GetSoftwareRoot() + "/prometheus/conf/prometheus.yml",
		templateFileName:    "yba-installer-prometheus.yml",
		DataDir:             common.GetBaseInstall() + "/data/prometheus",
		PromDir:             common.GetSoftwareRoot() + "/prometheus",
		cronScript: filepath.Join(
			common.GetInstallerSoftwareDir(), common.CronDir, "managePrometheus.sh"),
	}
}

// Component 2: Prometheus
type Prometheus struct {
	name    string
	version string
	prometheusDirectories
}

// NewPrometheus creates a new prometheus service struct.
func NewPrometheus(version string) Prometheus {
	return Prometheus{
		name:                  "prometheus",
		version:               version,
		prometheusDirectories: newPrometheusDirectories(),
	}
}

func (prom Prometheus) getSystemdFile() string {
	return prom.SystemdFileLocation
}

func (prom Prometheus) getConfFile() string {
	return prom.ConfFileLocation
}

// TemplateFile returns service's templated config file path
func (prom Prometheus) TemplateFile() string {
	return prom.templateFileName
}

// Name returns the name of the service.
func (prom Prometheus) Name() string {
	return prom.name
}

// Install the prometheus service.
func (prom Prometheus) Install() error {
	log.Info("Starting Prometheus install")
	config.GenerateTemplate(prom)
	prom.moveAndExtractPrometheusPackage()
	prom.createDataDirs()
	prom.createPrometheusSymlinks()

	//chown is not needed when we are operating under non-root, the user will already
	//have the necessary access.
	if common.HasSudoAccess() {
		userName := viper.GetString("service_username")
		common.Chown(common.GetSoftwareRoot()+"/prometheus", userName, userName, true)
	} else {
		prom.CreateCronJob()
	}

	if err := prom.Start(); err != nil {
		return err
	}
	log.Info("Finishing Prometheus install")
	return nil
}

// Start the prometheus service.
func (prom Prometheus) Start() error {

	if common.HasSudoAccess() {

		if out := shell.Run(common.Systemctl, "daemon-reload"); !out.SucceededOrLog() {
			return out.Error
		}
		if out := shell.Run(common.Systemctl, "enable", "prometheus"); !out.SucceededOrLog() {
			return out.Error
		}
		if out := shell.Run(common.Systemctl, "start", "prometheus"); !out.SucceededOrLog() {
			return out.Error
		}
		if out := shell.Run(common.Systemctl, "status", "prometheus"); !out.SucceededOrLog() {
			return out.Error
		}

	} else {
		out := shell.RunShell(prom.cronScript, common.GetSoftwareRoot(),
			common.GetDataRoot(),
			fmt.Sprint(viper.GetInt("prometheus.port")),
			fmt.Sprint(viper.GetInt("prometheus.maxSamples")),
			fmt.Sprint(viper.GetInt("prometheus.timeout")),
			fmt.Sprint(viper.GetInt("prometheus.maxConcurrency")),
			fmt.Sprint(viper.GetInt("prometheus.restartSeconds")),
			prom.version,
			"> /dev/null 2>&1 &",
		)
		if !out.SucceededOrLog() {
			return out.Error
		}
	}
	return nil
}

func (prom Prometheus) Stop() error {
	status, err := prom.Status()
	if err != nil {
		return err
	}
	if status.Status != common.StatusRunning {
		log.Debug(prom.name + " is already stopped")
		return nil
	}

	if common.HasSudoAccess() {
		if out := shell.Run(common.Systemctl, "stop", "prometheus"); !out.SucceededOrLog() {
			return out.Error
		}
	} else {

		// Delete the file used by the crontab bash script for monitoring.
		common.RemoveAll(common.GetSoftwareRoot() + "/prometheus/testfile")

		out := shell.Run("pgrep", "prometheus")
		if !out.SucceededOrLog() {
			return out.Error
		}
		// Need to stop the binary if it is running, can just do kill -9 PID (will work as the
		// process itself was started by a non-root user.)
		if strings.TrimSuffix(out.StdoutString(), "\n") != "" {
			pids := strings.Split(out.StdoutString(), "\n")
			for _, pid := range pids {
				log.Debug("kill prometheus pid: " + pid)
				if strings.TrimSuffix(pid, "\n") != "" {
					if out2 := shell.Run("kill", "-9", strings.TrimSuffix(pid, "\n")); !out2.SucceededOrLog() {
						return out2.Error
					}
				}
			}
		}
	}
	return nil
}

// Restart the prometheus service.
func (prom Prometheus) Restart() error {
	log.Info("Restarting prometheus..")

	if common.HasSudoAccess() {
		if out := shell.Run(common.Systemctl, "restart", "prometheus"); !out.SucceededOrLog() {
			return out.Error
		}
	} else {
		if err := prom.Stop(); err != nil {
			return err
		}
		if err := prom.Start(); err != nil {
			return err
		}
	}
	return nil
}

// Uninstall stops prometheus and optionally removes all data.
func (prom Prometheus) Uninstall(removeData bool) error {
	log.Info("Uninstalling prometheus")
	if err := prom.Stop(); err != nil {
		return err
	}

	if common.HasSudoAccess() {
		err := os.Remove(prom.SystemdFileLocation)
		if err != nil {
			pe := err.(*fs.PathError)
			if !errors.Is(pe.Err, fs.ErrNotExist) {
				log.Info(fmt.Sprintf("Error %s removing systemd service %s.",
					err.Error(), prom.SystemdFileLocation))
				return err
			}
		}
		// reload systemd daemon
		if out := shell.Run(common.Systemctl, "daemon-reload"); !out.SucceededOrLog() {
			return out.Error
		}
	}

	if removeData {
		err := common.RemoveAll(prom.prometheusDirectories.DataDir)
		if err != nil {
			log.Info(fmt.Sprintf("Error %s removing data dir %s.", err.Error(),
				prom.prometheusDirectories.DataDir))
			return err
		}
	}
	return nil
}

// Upgrade will upgrade prometheus and install it into the alt install directory.
// Upgrade will NOT restart the service, the old version is expected to still be runnins
func (prom Prometheus) Upgrade() error {
	prom.prometheusDirectories = newPrometheusDirectories()
	config.GenerateTemplate(prom) // No need to reload systemd, start takes care of that for us.
	prom.moveAndExtractPrometheusPackage()
	prom.createPrometheusSymlinks()

	//chown is not needed when we are operating under non-root, the user will already
	//have the necessary access.
	if common.HasSudoAccess() {
		userName := viper.GetString("service_username")
		common.Chown(common.GetSoftwareRoot()+"/prometheus", userName, userName, true)
	}

	//Crontab based monitoring for non-root installs.
	if !common.HasSudoAccess() {
		prom.CreateCronJob()
	}
	return prom.Start()
}

func (prom Prometheus) moveAndExtractPrometheusPackage() {

	packagesPath := common.GetInstallerSoftwareDir() + "/packages"

	srcPath := fmt.Sprintf(
		"%s/third-party/prometheus-%s.linux-amd64.tar.gz", common.GetInstallerSoftwareDir(),
		prom.version)
	dstPath := fmt.Sprintf(
		"%s/prometheus-%s.linux-amd64.tar.gz", packagesPath, prom.version)

	common.CopyFile(srcPath, dstPath)
	rExtract, errExtract := os.Open(dstPath)
	if errExtract != nil {
		log.Fatal("Error in starting the File Extraction process.")
	}
	defer rExtract.Close()

	extPackagePath := fmt.Sprintf(
		"%s/packages/prometheus-%s.linux-amd64", common.GetInstallerSoftwareDir(), prom.version)

	if _, err := os.Stat(extPackagePath); err == nil {
		log.Debug(extPackagePath + " already exists, skipping re-extract.")
	} else {
		if err := tar.Untar(rExtract, packagesPath, tar.WithMaxUntarSize(-1)); err != nil {
			log.Fatal(fmt.Sprintf("failed to extract file %s, error: %s", dstPath, err.Error()))
		}
		log.Debug(dstPath + " successfully extracted.")
	}

	if common.HasSudoAccess() {
		userName := viper.GetString("service_username")
		if err := common.Chown(packagesPath, userName, userName, true); err != nil {
			log.Fatal(fmt.Sprintf("failed to change ownership of %s: %s", packagesPath, err.Error()))
		}
	}
}

func (prom Prometheus) createDataDirs() {

	common.MkdirAll(prom.DataDir+"/storage", common.DirMode)
	common.MkdirAll(prom.DataDir+"/swamper_targets", common.DirMode)
	common.MkdirAll(prom.DataDir+"/swamper_rules", common.DirMode)
	log.Debug(prom.DataDir + "/storage /swamper_targets /swamper_rules" + " directories created.")

	// Create the log file
	common.Create(prom.DataDir + "/prometheus.log")

	if common.HasSudoAccess() {
		// Need to give the yugabyte user ownership of the entire postgres directory.
		userName := viper.GetString("service_username")
		common.Chown(prom.DataDir, userName, userName, true)
	}
}

func (prom Prometheus) createPrometheusSymlinks() {

	// Version specific promtheus that we untarred to packages.
	promPkg := fmt.Sprintf("%s/packages/prometheus-%s.linux-amd64",
		common.GetInstallerSoftwareDir(), prom.version)

	promBinaryDir := common.GetSoftwareRoot() + "/prometheus/bin"

	// Required for systemctl.
	if common.HasSudoAccess() {
		promBinaryDir = "/usr/local/bin"
	} else {
		// promBinaryDir doesn't exist for non-root mode, lets create it.
		common.MkdirAll(promBinaryDir, common.DirMode)
	}

	common.CreateSymlink(promPkg, promBinaryDir, "prometheus")
	common.CreateSymlink(promPkg, promBinaryDir, "promtool")
	common.CreateSymlink(promPkg, prom.PromDir, "consoles")
	common.CreateSymlink(promPkg, prom.PromDir, "console_libraries")

	if common.HasSudoAccess() {
		userName := viper.GetString("service_username")
		common.Chown(prom.PromDir, userName, userName, true)
	}

}

// Status prints out the header information for the
// Prometheus service specifically.
func (prom Prometheus) Status() (common.Status, error) {
	status := common.Status{
		Service:    prom.Name(),
		Port:       viper.GetInt("prometheus.port"),
		Version:    prom.version,
		ConfigLoc:  prom.ConfFileLocation,
		LogFileLoc: prom.DataDir + "/prometheus.log",
	}

	// Set the systemd service file location if one exists
	if common.HasSudoAccess() {
		status.ServiceFileLoc = prom.SystemdFileLocation
	} else {
		status.ServiceFileLoc = "N/A"
	}

	// Get the service status
	if common.HasSudoAccess() {
		props := systemd.Show(filepath.Base(prom.SystemdFileLocation), "LoadState", "SubState",
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
		out := shell.Run("pgrep", "prometheus")
		if out.Succeeded() {
			status.Status = common.StatusRunning
		} else if out.ExitCode == 1 {
			status.Status = common.StatusStopped
		} else {
			return status, out.Error
		}
	}
	return status, nil
}

// CreateCronJob creates the cron job for managing prometheus with cron script in non-root.
func (prom Prometheus) CreateCronJob() {
	bashCmd := fmt.Sprintf(
		"(crontab -l 2>/dev/null; echo \"@reboot %s %s %s %s %s %s %s %s %s \") | sort - | uniq - | "+
			"crontab - ",
		prom.cronScript,
		common.GetSoftwareRoot(),
		common.GetDataRoot(),
		config.GetYamlPathData("prometheus.port"),
		config.GetYamlPathData("prometheus.maxConcurrency"),
		config.GetYamlPathData("prometheus.maxSamples"),
		config.GetYamlPathData("prometheus.timeout"),
		config.GetYamlPathData("prometheus.restartSeconds"),
		prom.version,
	)
	shell.Run("bash", "-c", bashCmd)
}
