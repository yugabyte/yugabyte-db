/*
 * Copyright (c) YugaByte, Inc.
 */

package cmd

import (
	"fmt"
	"os"
	"path/filepath"
	"strings"

	"github.com/fluxcd/pkg/tar"
	"github.com/spf13/viper"

	"github.com/yugabyte/yugabyte-db/managed/yba-installer/common"
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
		ConfFileLocation:    common.GetInstallRoot() + "/prometheus/conf/prometheus.yml",
		templateFileName:    "yba-installer-prometheus.yml",
		DataDir:             common.GetBaseInstall() + "/data/prometheus",
		PromDir:             common.GetInstallRoot() + "/prometheus",
		cronScript: filepath.Join(
			common.GetInstallVersionDir(), common.CronDir, "managePrometheus.sh"),
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
func (prom Prometheus) Install() {
	log.Info("Starting Prometheus install")
	config.GenerateTemplate(prom)
	prom.moveAndExtractPrometheusPackage()
	prom.createDataDirs()
	prom.createPrometheusSymlinks()

	//chown is not needed when we are operating under non-root, the user will already
	//have the necessary access.
	if common.HasSudoAccess() {
		userName := viper.GetString("service_username")
		common.Chown(common.GetInstallRoot()+"/prometheus", userName, userName, true)

	}

	//Crontab based monitoring for non-root installs.
	if !common.HasSudoAccess() {
		prom.CreateCronJob()
	}

	prom.Start()
	log.Info("Finishing Prometheus install")
}

// Start the prometheus service.
func (prom Prometheus) Start() {

	if common.HasSudoAccess() {

		common.ExecuteBashCommand(common.Systemctl, []string{"daemon-reload"})
		common.ExecuteBashCommand(common.Systemctl, []string{"start", "prometheus"})
		common.ExecuteBashCommand(common.Systemctl, []string{"status", "prometheus"})

	} else {
		bashCmd := fmt.Sprintf("%s %d %d %d %d %d > /dev/null 2>&1 &",
			prom.cronScript,
			viper.GetInt("prometheus.port"),
			viper.GetInt("prometheus.maxConcurrency"),
			viper.GetInt("prometheus.maxSamples"),
			viper.GetInt("prometheus.timeout"),
			viper.GetInt("prometheus.restartSeconds"),
		)
		command1 := "bash"
		arg1 := []string{"-c", bashCmd}

		common.ExecuteBashCommand(command1, arg1)

	}

}

func (prom Prometheus) Stop() {

	if common.HasSudoAccess() {

		arg1 := []string{"stop", "prometheus"}
		common.ExecuteBashCommand(common.Systemctl, arg1)

	} else {

		// Delete the file used by the crontab bash script for monitoring.
		os.RemoveAll(common.GetInstallRoot() + "/prometheus/testfile")

		commandCheck0 := "bash"
		argCheck0 := []string{"-c", "pgrep prometheus"}
		out0, _ := common.ExecuteBashCommand(commandCheck0, argCheck0)

		// Need to stop the binary if it is running, can just do kill -9 PID (will work as the
		// process itself was started by a non-root user.)
		if strings.TrimSuffix(string(out0), "\n") != "" {
			pids := strings.Split(string(out0), "\n")
			for _, pid := range pids {
				argStop := []string{"-c", "kill -9 " + strings.TrimSuffix(pid, "\n")}
				common.ExecuteBashCommand(commandCheck0, argStop)
			}
		}
	}
}

// Restart the prometheus service.
func (prom Prometheus) Restart() {

	if common.HasSudoAccess() {

		arg1 := []string{"restart", "prometheus"}
		common.ExecuteBashCommand(common.Systemctl, arg1)

	} else {

		prom.Stop()
		prom.Start()

	}

}

// Uninstall uninstalls prometheus and optionally removes all data.
func (prom Prometheus) Uninstall(removeData bool) {
	prom.Stop()
}

// Upgrade will upgrade prometheus and install it into the alt install directory.
// Upgrade will NOT restart the service, the old version is expected to still be runnins
func (prom Prometheus) Upgrade() {
	log.Info("Starting Prometheus upgrade")
	prom.prometheusDirectories = newPrometheusDirectories()
	config.GenerateTemplate(prom) // No need to reload systemd, start takes care of that for us.
	prom.moveAndExtractPrometheusPackage()
	prom.createPrometheusSymlinks()

	//chown is not needed when we are operating under non-root, the user will already
	//have the necessary access.
	if common.HasSudoAccess() {
		userName := viper.GetString("service_username")
		common.Chown(common.GetInstallRoot()+"/prometheus", userName, userName, true)
	}

	//Crontab based monitoring for non-root installs.
	if !common.HasSudoAccess() {
		prom.CreateCronJob()
	}
	prom.Start()
	log.Info("Finishing Prometheus upgrade")
}

func (prom Prometheus) moveAndExtractPrometheusPackage() {

	srcPath := fmt.Sprintf(
		"%s/third-party/prometheus-%s.linux-amd64.tar.gz", common.GetInstallVersionDir(), prom.version)
	dstPath := fmt.Sprintf(
		"%s/packages/prometheus-%s.linux-amd64.tar.gz", common.GetInstallVersionDir(), prom.version)

	common.CopyFileGolang(srcPath, dstPath)
	rExtract, errExtract := os.Open(dstPath)
	if errExtract != nil {
		log.Fatal("Error in starting the File Extraction process.")
	}
	defer rExtract.Close()

	extPackagePath := fmt.Sprintf(
		"%s/packages/prometheus-%s.linux-amd64", common.GetInstallVersionDir(), prom.version)
	if _, err := os.Stat(extPackagePath); err == nil {
		log.Debug(extPackagePath + " already exists, skipping re-extract.")
	} else {
		if err := tar.Untar(rExtract, common.GetInstallVersionDir()+"/packages",
			tar.WithMaxUntarSize(-1)); err != nil {
			log.Fatal(fmt.Sprintf("failed to extract file %s, error: %s", dstPath, err.Error()))
		}
		log.Debug(dstPath + " successfully extracted.")
	}

}

func (prom Prometheus) createDataDirs() {

	os.MkdirAll(prom.DataDir+"/storage", os.ModePerm)
	os.MkdirAll(prom.DataDir+"/swamper_targets", os.ModePerm)
	os.MkdirAll(prom.DataDir+"/swamper_rules", os.ModePerm)
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
		common.GetInstallVersionDir(), prom.version)

	promBinaryDir := common.GetInstallRoot() + "/prometheus/bin"

	// Required for systemctl.
	if common.HasSudoAccess() {
		promBinaryDir = "/usr/local/bin"
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
func (prom Prometheus) Status() common.Status {
	status := common.Status{
		Service:   prom.Name(),
		Port:      viper.GetInt("prometheus.port"),
		Version:   prom.version,
		ConfigLoc: prom.ConfFileLocation,
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
		command := "bash"
		args := []string{"-c", "pgrep prometheus"}
		out0, _ := common.ExecuteBashCommand(command, args)

		if strings.TrimSuffix(string(out0), "\n") != "" {
			status.Status = common.StatusRunning
		} else {
			status.Status = common.StatusStopped
		}
	}
	return status
}

// CreateCronJob creates the cron job for managing prometheus with cron script in non-root.
func (prom Prometheus) CreateCronJob() {
	bashCmd := fmt.Sprintf(
		"(crontab -l 2>/dev/null; echo \"@reboot %s %s %s %s %s %s \") | sort - | uniq - | crontab - ",
		prom.cronScript,
		config.GetYamlPathData("prometheus.port"),
		config.GetYamlPathData("prometheus.maxConcurrency"),
		config.GetYamlPathData("prometheus.maxSamples"),
		config.GetYamlPathData("prometheus.timeout"),
		config.GetYamlPathData("prometheus.restartSeconds"),
	)
	common.ExecuteBashCommand("bash", []string{"-c", bashCmd})
}
