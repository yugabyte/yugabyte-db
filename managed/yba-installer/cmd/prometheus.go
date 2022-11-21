/*
 * Copyright (c) YugaByte, Inc.
 */

package cmd

import (
	"errors"
	"fmt"
	"os"
	"strings"

	"github.com/fluxcd/pkg/tar"
	"github.com/spf13/viper"
)

// Component 2: Prometheus
type Prometheus struct {
	Name                string
	SystemdFileLocation string
	ConfFileLocation    string
	templateFileName    string
	Version             string
	isUpgrade           bool
	DataDir             string
	PromDir             string
}

// TODO: Pass this in from common when defining services? At that point can pull from input.yaml
// var DataDir = INSTALL_ROOT + "/data/prometheus"
// var PromDir = INSTALL_ROOT + "/prometheus"

// Method of the Component
// Interface are implemented by
// the Prometheus struct and customizable
// for each specific service.

func NewPrometheus(installRoot, version string, isUpgrade bool) Prometheus {
	return Prometheus{
		"prometheus",
		SYSTEMD_DIR + "/prometheus.service",
		INSTALL_ROOT + "/prometheus/conf/prometheus.yml",
		"yba-installer-prometheus.yml",
		version,
		isUpgrade,
		// data directory
		INSTALL_ROOT + "/data/prometheus",
		// prometheus code/conf directory
		INSTALL_ROOT + "/prometheus"}
}

func (prom Prometheus) SetUpPrereqs() {
	prom.moveAndExtractPrometheusPackage(prom.Version)
}

func (prom Prometheus) Install() {
	prom.createDataDirs()
	prom.createPrometheusSymlinks(prom.Version, prom.isUpgrade)

	//chown is not needed when we are operating under non-root, the user will already
	//have the necesary access.
	if hasSudoAccess() {

		ExecuteBashCommand(CHOWN, []string{"yugabyte:yugabyte", "-R",
			INSTALL_ROOT + "/prometheus"})

	}

	//Crontab based monitoring for non-root installs.
	if !hasSudoAccess() {
		prom.CreateCronJob()
	}
}

func (prom Prometheus) Start() {

	if hasSudoAccess() {

		ExecuteBashCommand(SYSTEMCTL, []string{"daemon-reload"})
		ExecuteBashCommand(SYSTEMCTL, []string{"start", "prometheus"})
		ExecuteBashCommand(SYSTEMCTL, []string{"status", "prometheus"})

	} else {
		scriptPath := INSTALL_VERSION_DIR + "/crontabScripts/manage" + prom.Name + "NonRoot.sh"
		bashCmd := fmt.Sprintf("%s %d %d %d %d %d > /dev/null 2>&1 &",
			scriptPath,
			viper.GetInt("prometheus.externalPort"),
			viper.GetInt("prometheus.maxConcurrency"),
			viper.GetInt("prometheus.maxSamples"),
			viper.GetInt("prometheus.timeout"),
			viper.GetInt("prometheus.restartSeconds"),
		)
		command1 := "bash"
		arg1 := []string{"-c", bashCmd}

		ExecuteBashCommand(command1, arg1)

	}

}

func (prom Prometheus) Stop() {

	if hasSudoAccess() {

		arg1 := []string{"stop", "prometheus"}
		ExecuteBashCommand(SYSTEMCTL, arg1)

	} else {

		// Delete the file used by the crontab bash script for monitoring.
		os.RemoveAll(INSTALL_ROOT + "/prometheus/testfile")

		commandCheck0 := "bash"
		argCheck0 := []string{"-c", "pgrep prometheus"}
		out0, _ := ExecuteBashCommand(commandCheck0, argCheck0)

		// Need to stop the binary if it is running, can just do kill -9 PID (will work as the
		// process itself was started by a non-root user.)
		if strings.TrimSuffix(string(out0), "\n") != "" {
			pids := strings.Split(string(out0), "\n")
			for _, pid := range pids {
				argStop := []string{"-c", "kill -9 " + strings.TrimSuffix(pid, "\n")}
				ExecuteBashCommand(commandCheck0, argStop)
			}
		}
	}
}

func (prom Prometheus) Restart() {

	if hasSudoAccess() {

		arg1 := []string{"restart", "prometheus"}
		ExecuteBashCommand(SYSTEMCTL, arg1)

	} else {

		prom.Stop()
		prom.Start()

	}

}

func (prom Prometheus) getSystemdFile() string {
	return prom.SystemdFileLocation
}

func (prom Prometheus) getConfFile() string {
	return prom.ConfFileLocation
}

func (prom Prometheus) getTemplateFile() string {
	return prom.templateFileName
}

// Per current cleanup.sh script.
func (prom Prometheus) Uninstall(removeData bool) {
	prom.Stop()
	RemoveAllExceptDataVolumes([]string{"prometheus"})
}

func (prom Prometheus) VersionInfo() string {
	return prom.Version
}

func (prom Prometheus) moveAndExtractPrometheusPackage(ver string) {

	srcPath := INSTALL_VERSION_DIR + "/third-party/prometheus-" + ver + ".linux-amd64.tar.gz"
	dstPath := INSTALL_VERSION_DIR + "/packages/prometheus-" + ver + ".linux-amd64.tar.gz"

	CopyFileGolang(srcPath, dstPath)
	rExtract, errExtract := os.Open(dstPath)
	if errExtract != nil {
		LogError("Error in starting the File Extraction process.")
	}

	path_package_extracted := INSTALL_VERSION_DIR + "/packages/prometheus-" + ver + ".linux-amd64"

	if _, err := os.Stat(path_package_extracted); err == nil {
		LogDebug(path_package_extracted + " already exists, skipping re-extract.")
	} else {
		if err := tar.Untar(rExtract, INSTALL_VERSION_DIR+"/packages",
			tar.WithMaxUntarSize(-1)); err != nil {
			LogError(fmt.Sprintf("failed to extract file %s, error: %s", dstPath, err.Error()))
		}
		LogDebug(dstPath + " successfully extracted.")
	}

}

func (prom Prometheus) createDataDirs() {

	os.MkdirAll(prom.DataDir+"/storage", os.ModePerm)
	os.MkdirAll(prom.DataDir+"/swamper_targets", os.ModePerm)
	os.MkdirAll(prom.DataDir+"/swamper_rules", os.ModePerm)
	LogDebug(prom.DataDir + "/storage /swamper_targets /swamper_rules" + " directories created.")

	// Create the log file
	Create(prom.DataDir + "/prometheus.log")

	if hasSudoAccess() {
		// Need to give the yugabyte user ownership of the entire postgres directory.

		ExecuteBashCommand(CHOWN,
			[]string{"-R", "yugabyte:yugabyte", prom.DataDir})
	}
}

func (prom Prometheus) createPrometheusSymlinks(ver string, isUpgrade bool) {

	pkgPromBinary := INSTALL_VERSION_DIR + "/packages/prometheus-" + ver + ".linux-amd64/prometheus"

	promBinaryLink := INSTALL_ROOT + "/prometheus/bin/prometheus"

	// Required for systemctl.
	if hasSudoAccess() {
		promBinaryLink = "/usr/local/bin/prometheus"

	}

	arg1 := []string{"-sf", pkgPromBinary, promBinaryLink}

	if _, err := os.Stat(promBinaryLink); err == nil {
		os.Remove(promBinaryLink)
		ExecuteBashCommand(LN, arg1)
	} else if errors.Is(err, os.ErrNotExist) {
		ExecuteBashCommand(LN, arg1)
	}

	if hasSudoAccess() {
		os.Chmod(promBinaryLink, os.ModePerm)
	}

	promBin := INSTALL_ROOT + "/prometheus/bin/"
	os.MkdirAll(promBin, os.ModePerm)
	pkgPromToolPath := INSTALL_VERSION_DIR + "/packages/prometheus-" + ver + ".linux-amd64/promtool"

	arg3 := []string{"-sf", pkgPromToolPath, promBin + "promtool"}

	if _, err := os.Stat(INSTALL_ROOT + "/prometheus/bin/promtool"); err == nil {
		os.Remove(INSTALL_ROOT + "/prometheus/bin/promtool")
		ExecuteBashCommand(LN, arg3)
	} else if errors.Is(err, os.ErrNotExist) {
		ExecuteBashCommand(LN, arg3)
	}

	pkgConsoleDir := INSTALL_VERSION_DIR + "/packages/prometheus-" + ver + ".linux-amd64/consoles/"

	arg5 := []string{"-sf", pkgConsoleDir, prom.PromDir}

	if _, err := os.Stat(INSTALL_ROOT + "/prometheus/consoles"); err == nil {
		os.Remove(INSTALL_ROOT + "/prometheus/consoles")
		ExecuteBashCommand(LN, arg5)
	} else if errors.Is(err, os.ErrNotExist) {
		ExecuteBashCommand(LN, arg5)
	}

	pkgConsoleLibraryDir := INSTALL_VERSION_DIR + "/packages/prometheus-" +
		ver + ".linux-amd64/console_libraries/"

	arg7 := []string{"-sf", pkgConsoleLibraryDir, prom.PromDir}

	if _, err := os.Stat(INSTALL_ROOT + "/prometheus/console_libraries"); err == nil {
		os.Remove(INSTALL_ROOT + "/prometheus/console_libraries")
		ExecuteBashCommand(LN, arg7)
	} else if errors.Is(err, os.ErrNotExist) {
		ExecuteBashCommand(LN, arg7)
	}

	if hasSudoAccess() {
		Chown(prom.PromDir, "yugabyte", "yugabyte", true)
	}

}

// Status prints out the header information for the
// Prometheus service specifically.
func (prom Prometheus) Status() {

	name := "prometheus"
	port := fmt.Sprintf("%d", viper.GetInt("prometheus.externalPort"))

	runningStatus := ""

	if hasSudoAccess() {

		args := []string{"is-active", name}
		runningStatus, _ = ExecuteBashCommand(SYSTEMCTL, args)

		runningStatus = strings.ReplaceAll(strings.TrimSuffix(runningStatus, "\n"), " ", "")

		// For display purposes.
		if runningStatus != "active" {

			runningStatus = "inactive"
		}

	} else {

		command := "bash"
		args := []string{"-c", "pgrep " + name}
		out0, _ := ExecuteBashCommand(command, args)

		if strings.TrimSuffix(string(out0), "\n") != "" {
			runningStatus = "active"
		} else {
			runningStatus = "inactive"
		}
	}

	systemdLoc := "N/A"

	if hasSudoAccess() {

		systemdLoc = prom.SystemdFileLocation
	}

	outString := name + "\t" + prom.Version + "\t" + port +
		"\t" + prom.ConfFileLocation + "\t" + systemdLoc +
		"\t" + runningStatus + "\t"

	fmt.Fprintln(statusOutput, outString)

}

func (prom Prometheus) CreateCronJob() {
	scriptPath := INSTALL_VERSION_DIR + "/crontabScripts/manage" + prom.Name + "NonRoot.sh"
	bashCmd := fmt.Sprintf(
		"(crontab -l 2>/dev/null; echo \"@reboot %s %d %d %d %d %d \") | sort - | uniq - | crontab - ",
		scriptPath,
		viper.GetInt("prometheus.externalPort"),
		viper.GetInt("prometheus.maxConcurrency"),
		viper.GetInt("prometheus.maxSamples"),
		viper.GetInt("prometheus.timeout"),
		viper.GetInt("prometheus.restartSeconds"),
	)
	ExecuteBashCommand("bash", []string{"-c", bashCmd})
}
