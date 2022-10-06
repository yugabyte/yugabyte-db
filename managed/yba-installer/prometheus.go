/*
 * Copyright (c) YugaByte, Inc.
 */

package main

import (
	"errors"
	"fmt"
	"os"
	"strings"
)

// Component 2: Prometheus
type Prometheus struct {
	Name                string
	SystemdFileLocation string
	ConfFileLocation    string
	Version             string
	IsUpgrade           bool
}

// Method of the Component
// Interface are implemented by
// the Prometheus struct and customizable
// for each specific service.

func (prom Prometheus) SetUpPrereqs() {
	prom.moveAndExtractPrometheusPackage(prom.Version)
}

func (prom Prometheus) Install() {
	createPrometheusUser(prom.IsUpgrade)
	prom.createPrometheusSymlinks(prom.Version, prom.IsUpgrade)

	//chown is not needed when we are operating under non-root, the user will already
	//have the necesary access.
	if hasSudoAccess() {

		ExecuteBashCommand("chown", []string{"prometheus:prometheus", "-R",
			INSTALL_ROOT + "/prometheus/bin/"})

		ExecuteBashCommand("chown", []string{"prometheus:prometheus", "-R",
			INSTALL_ROOT + "/prometheus/conf/"})

		ExecuteBashCommand("chown", []string{"prometheus:prometheus", "-R",
			INSTALL_ROOT + "/prometheus/storage/"})
	}

	//Crontab based monitoring for non-root installs.
	if !hasSudoAccess() {
		prom.CreateCronJob()
	}
}

func (prom Prometheus) Start() {

	if hasSudoAccess() {

		arg1 := []string{"daemon-reload"}
		ExecuteBashCommand(SYSTEMCTL, arg1)

		arg2 := []string{"start", "prometheus"}
		ExecuteBashCommand(SYSTEMCTL, arg2)

		arg3 := []string{"status", "prometheus"}
		ExecuteBashCommand(SYSTEMCTL, arg3)

	} else {

		maxConcurrency := getYamlPathData(".prometheus.maxConcurrency")
		maxSamples := getYamlPathData(".prometheus.maxSamples")
		timeout := getYamlPathData(".prometheus.timeout")
		externalPort := getYamlPathData(".prometheus.externalPort")

		scriptPath := INSTALL_VERSION_DIR + "/crontabScripts/manage" + prom.Name + "NonRoot.sh"

		command1 := "bash"
		arg1 := []string{"-c", scriptPath + " " + externalPort + " " + maxConcurrency + " " + maxSamples +
			" " + timeout + " " + " > /dev/null 2>&1 &"}

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

func (prom Prometheus) GetSystemdFile() string {
	return prom.SystemdFileLocation
}

func (prom Prometheus) GetConfFile() string {
	return prom.ConfFileLocation
}

//Per current cleanup.sh script.
func (prom Prometheus) Uninstall() {
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
		Untar(rExtract, INSTALL_VERSION_DIR+"/packages")
		LogDebug(dstPath + " successfully extracted.")
	}

}

func createPrometheusUser(isUpgrade bool) {

	if !isUpgrade {

		command1 := "bash"
		args1 := []string{"-c", "id -u prometheus"}
		_, err := ExecuteBashCommand(command1, args1)

		if err != nil {

			if hasSudoAccess() {
				command2 := "useradd"
				arg2 := []string{"--no-create-home", "--shell", "/bin/false", "prometheus"}
				ExecuteBashCommand(command2, arg2)
			}

		} else {
			LogDebug("User prometheus already exists or install is non-root, skipping user creation.")
		}

		os.MkdirAll(INSTALL_ROOT+"/prometheus", os.ModePerm)
		LogDebug(INSTALL_ROOT + "/prometheus" + " directory successfully created.")

		os.MkdirAll(INSTALL_ROOT+"/prometheus/storage/", os.ModePerm)
		LogDebug(INSTALL_ROOT + "/prometheus/storage/" + " directory successfully created.")

		//Create the prometheus.log file so that we can start prometheus as a background process for non-root.
		//Recursive create so that we create the bin directory if it does not exists before creating prometheus.log.
		Create(INSTALL_ROOT + "/prometheus/bin/prometheus.log")

		//Make the swamper_targets Directory for prometheus
		os.MkdirAll(INSTALL_ROOT+"/prometheus/swamper_targets", os.ModePerm)

	}

}

func (prom Prometheus) createPrometheusSymlinks(ver string, isUpgrade bool) {

	command1 := "ln"
	path1a := INSTALL_VERSION_DIR + "/packages/prometheus-" + ver + ".linux-amd64/prometheus"

	path1b := INSTALL_ROOT + "/prometheus/bin/prometheus"

	// Required for systemctl.
	if hasSudoAccess() {
		path1b = "/usr/local/bin/prometheus"

	}

	arg1 := []string{"-sf", path1a, path1b}

	if _, err := os.Stat(path1b); err == nil {
		os.Remove(path1b)
		ExecuteBashCommand(command1, arg1)
	} else if errors.Is(err, os.ErrNotExist) {
		ExecuteBashCommand(command1, arg1)
	}

	if hasSudoAccess() {
		os.Chmod(path1b, os.ModePerm)
	}

	if hasSudoAccess() {
		command2 := "chown"
		arg2 := []string{"prometheus:prometheus", path1b}
		ExecuteBashCommand(command2, arg2)
	}

	command3 := "ln"
	path3a := INSTALL_VERSION_DIR + "/packages/prometheus-" + ver + ".linux-amd64/promtool"
	path3b := INSTALL_ROOT + "/prometheus/bin/promtool"

	arg3 := []string{"-sf", path3a, path3b}

	if _, err := os.Stat(INSTALL_ROOT + "/prometheus/bin/promtool"); err == nil {
		os.Remove(INSTALL_ROOT + "/prometheus/bin/promtool")
		ExecuteBashCommand(command3, arg3)
	} else if errors.Is(err, os.ErrNotExist) {
		ExecuteBashCommand(command3, arg3)
	}

	if hasSudoAccess() {
		command4 := "chown"
		arg4 := []string{"prometheus:prometheus", INSTALL_ROOT + "/prometheus/bin/promtool"}
		ExecuteBashCommand(command4, arg4)
	}

	command5 := "ln"
	path5a := INSTALL_VERSION_DIR + "/packages/prometheus-" + ver + ".linux-amd64/consoles/"
	path5b := INSTALL_ROOT + "/prometheus/"

	arg5 := []string{"-sf", path5a, path5b}

	if _, err := os.Stat(INSTALL_ROOT + "/prometheus/consoles"); err == nil {
		os.Remove(INSTALL_ROOT + "/prometheus/consoles")
		ExecuteBashCommand(command5, arg5)
	} else if errors.Is(err, os.ErrNotExist) {
		ExecuteBashCommand(command5, arg5)
	}

	if hasSudoAccess() {
		command6 := "chown"
		arg6 := []string{"-R", "prometheus:prometheus", INSTALL_ROOT + "/prometheus/consoles"}
		ExecuteBashCommand(command6, arg6)
	}

	command7 := "ln"
	path7a := INSTALL_VERSION_DIR + "/packages/prometheus-" + ver + ".linux-amd64/console_libraries/"
	path7b := INSTALL_ROOT + "/prometheus/"

	arg7 := []string{"-sf", path7a, path7b}

	if _, err := os.Stat(INSTALL_ROOT + "/prometheus/console_libraries"); err == nil {
		os.Remove(INSTALL_ROOT + "/prometheus/console_libraries")
		ExecuteBashCommand(command7, arg7)
	} else if errors.Is(err, os.ErrNotExist) {
		ExecuteBashCommand(command7, arg7)
	}

	if hasSudoAccess() {
		command8 := "chown"
		arg8 := []string{"-R", "prometheus:prometheus", INSTALL_ROOT + "/prometheus/console_libraries"}
		ExecuteBashCommand(command8, arg8)
	}

}

// Status prints out the header information for the
// Prometheus service specifically.
func (prom Prometheus) Status() {

	name := "prometheus"
	port := getYamlPathData(".prometheus.externalPort")

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
	maxConcurrency := getYamlPathData(".prometheus.maxConcurrency")
	maxSamples := getYamlPathData(".prometheus.maxSamples")
	timeout := getYamlPathData(".prometheus.timeout")
	externalPort := getYamlPathData(".prometheus.externalPort")
	scriptPath := INSTALL_VERSION_DIR + "/crontabScripts/manage" + prom.Name + "NonRoot.sh"
	ExecuteBashCommand("bash", []string{"-c",
		"(crontab -l 2>/dev/null; echo \"@reboot " + scriptPath + " " + externalPort + " " +
			maxConcurrency + " " + maxSamples + " " + timeout + "\") | sort - | uniq - | crontab - "})
}
