/*
 * Copyright (c) YugaByte, Inc.
 */

package main

import (
	"errors"
	"fmt"
	"io/ioutil"
	"os"
	"strings"
)

// Component 3: Platform
type Platform struct {
	Name                string
	SystemdFileLocation string
	ConfFileLocation    string
	Version             string
	CorsOrigin          string
	UseOIDCSso          bool
}

// Method of the Component
// Interface are implemented by
// the Platform struct and customizable
// for each specific service.

func (plat Platform) Install() {

	createNecessaryDirectories(plat.Version)
	createDevopsAndYugawareDirectories(plat.Version)
	untarDevopsAndYugawarePackages(plat.Version)
	copyYugabyteReleaseFile(plat.Version)
	renameAndCreateSymlinks(plat.Version)
	configureConfHTTPS()

	//Create the platform.log file so that we can start platform as
	//a background process for non-root.
	os.Create(INSTALL_ROOT + "/yb-platform/yugaware/bin/platform.log")

	//Crontab based monitoring for non-root installs.
	if !hasSudoAccess() {
		plat.CreateCronJob()
	}

	// Change ownership of the data directory so that all universes can
	// be properly created as the Yugabyte User, under the Sudo method
	// of installation.
	if hasSudoAccess() {

		ExecuteBashCommand("chown", []string{"yugabyte:yugabyte", "-R", INSTALL_ROOT + "/yb-platform"})
		swamperTargetsDir := []string{"yugabyte:yugabyte", "-R", INSTALL_ROOT + "/prometheus/swamper_targets"}
		swamperRulesDir := []string{"yugabyte:yugabyte", "-R", INSTALL_ROOT + "/prometheus/swamper_rules"}
		ExecuteBashCommand("chown", swamperTargetsDir)
		ExecuteBashCommand("chown", swamperRulesDir)

	}

	// At the end of the installation, we rename .installStarted to .installCompleted, to signify that the
	// install has finished succesfully.
	MoveFileGolang(INSTALL_ROOT+"/.installStarted", INSTALL_ROOT+"/.installCompleted")

}

func createNecessaryDirectories(version string) {

	os.MkdirAll(INSTALL_ROOT+"/yb-platform/releases/"+version, os.ModePerm)
	os.MkdirAll(INSTALL_ROOT+"/prometheus/swamper_targets", os.ModePerm)
	os.MkdirAll(INSTALL_ROOT+"/prometheus/swamper_rules", os.ModePerm)
	os.MkdirAll(INSTALL_ROOT+"/yb-platform/data", os.ModePerm)
	os.MkdirAll(INSTALL_ROOT+"/yb-platform/third-party", os.ModePerm)

}

func createDevopsAndYugawareDirectories(version string) {

	packageFolder := "yugabyte-" + version
	os.MkdirAll(INSTALL_VERSION_DIR+"/packages/"+packageFolder+"/devops", os.ModePerm)
	os.MkdirAll(INSTALL_VERSION_DIR+"/packages/"+packageFolder+"/yugaware", os.ModePerm)

}

func untarDevopsAndYugawarePackages(version string) {

	packageFolder := "yugabyte-" + version
	packageFolderPath := INSTALL_VERSION_DIR + "/packages/" + packageFolder

	files, err := ioutil.ReadDir(packageFolderPath)
	if err != nil {
		LogError("Error: " + err.Error() + ".")
	}

	for _, f := range files {
		if strings.Contains(f.Name(), "devops") {

			devopsTgzName := f.Name()
			devopsTgzPath := packageFolderPath + "/" + devopsTgzName
			rExtract, errExtract := os.Open(devopsTgzPath)
			if errExtract != nil {
				LogError("Error in starting the File Extraction process.")
			}

			Untar(rExtract, packageFolderPath+"/devops")

		} else if strings.Contains(f.Name(), "yugaware") {

			yugawareTgzName := f.Name()
			yugawareTgzPath := packageFolderPath + "/" + yugawareTgzName
			rExtract, errExtract := os.Open(yugawareTgzPath)
			if errExtract != nil {
				LogError("Error in starting the File Extraction process.")
			}

			Untar(rExtract, packageFolderPath+"/yugaware")

		}
	}

}

func copyYugabyteReleaseFile(version string) {

	packageFolder := "yugabyte-" + version
	packageFolderPath := INSTALL_VERSION_DIR + "/packages/" + packageFolder

	files, err := ioutil.ReadDir(packageFolderPath)
	if err != nil {
		LogError("Error: " + err.Error() + ".")
	}

	for _, f := range files {
		if strings.Contains(f.Name(), "yugabyte") {

			yugabyteTgzName := f.Name()
			yugabyteTgzPath := packageFolderPath + "/" + yugabyteTgzName
			CopyFileGolang(yugabyteTgzPath,
				INSTALL_ROOT+"/yb-platform/releases/"+version+"/"+yugabyteTgzName)

		}
	}
}

func renameAndCreateSymlinks(version string) {

	packageFolder := "yugabyte-" + version
	packageFolderPath := INSTALL_VERSION_DIR + "/packages/" + packageFolder

	command1 := "ln"
	path1a := packageFolderPath + "/yugaware"

	path1b := INSTALL_ROOT + "/yb-platform/yugaware"
	arg1 := []string{"-sf", path1a, path1b}

	if _, err := os.Stat(path1b); err == nil {
		pathBackup := packageFolderPath + "/yugaware_backup"
		MoveFileGolang(path1b, pathBackup)
	} else if errors.Is(err, os.ErrNotExist) {
		ExecuteBashCommand(command1, arg1)
	}

	command2 := "ln"
	path2a := packageFolderPath + "/devops"
	path2b := INSTALL_ROOT + "/yb-platform/devops"
	arg2 := []string{"-sf", path2a, path2b}

	if _, err := os.Stat(path2b); err == nil {
		pathBackup := packageFolderPath + "/devops_backup"
		MoveFileGolang(path2b, pathBackup)
	} else if errors.Is(err, os.ErrNotExist) {
		ExecuteBashCommand(command2, arg2)
	}

}

func (plat Platform) Start() {

	if hasSudoAccess() {

		arg1 := []string{"daemon-reload"}
		ExecuteBashCommand(SYSTEMCTL, arg1)

		arg2 := []string{"enable", "yb-platform.service"}
		ExecuteBashCommand(SYSTEMCTL, arg2)

		arg3 := []string{"start", "yb-platform.service"}
		ExecuteBashCommand(SYSTEMCTL, arg3)

		arg4 := []string{"status", "yb-platform.service"}
		ExecuteBashCommand(SYSTEMCTL, arg4)

	} else {

		containerExposedPort := getYamlPathData(".platform.containerExposedPort")

		scriptPath := INSTALL_VERSION_DIR + "/crontabScripts/manage" + plat.Name + "NonRoot.sh"

		command1 := "bash"
		arg1 := []string{"-c", scriptPath + " " + INSTALL_VERSION_DIR + " " + containerExposedPort +
			" > /dev/null 2>&1 &"}

		ExecuteBashCommand(command1, arg1)

	}

}

func (plat Platform) Stop() {

	if hasSudoAccess() {

		arg1 := []string{"stop", "yb-platform.service"}
		ExecuteBashCommand(SYSTEMCTL, arg1)

	} else {

		// Delete the file used by the crontab bash script for monitoring.
		os.RemoveAll(INSTALL_ROOT + "/yb-platform/testfile")

		commandCheck0 := "bash"
		argCheck0 := []string{"-c", "pgrep -fl yb-platform"}
		out0, _ := ExecuteBashCommand(commandCheck0, argCheck0)

		// Need to stop the binary if it is running, can just do kill -9 PID (will work as the
		// process itself was started by a non-root user.)

		// Java check because pgrep will count the execution of yba-ctl as a process itself.
		if strings.TrimSuffix(string(out0), "\n") != "" {
			pids := strings.Split(string(out0), "\n")
			for _, pid := range pids {
				if strings.Contains(pid, "java") {
					argStop := []string{"-c", "kill -9 " + strings.TrimSuffix(pid, "\n")}
					ExecuteBashCommand(commandCheck0, argStop)
				}
			}
		}
	}
}

func (plat Platform) Restart() {

	if hasSudoAccess() {

		arg1 := []string{"restart", "yb-platform.service"}
		ExecuteBashCommand(SYSTEMCTL, arg1)

	} else {

		plat.Stop()
		plat.Start()

	}

}

func (plat Platform) GetSystemdFile() string {
	return plat.SystemdFileLocation
}

func (plat Platform) GetConfFile() string {
	return plat.ConfFileLocation
}

// Per current cleanup.sh script.
func (plat Platform) Uninstall() {
	plat.Stop()
	RemoveAllExceptDataVolumes([]string{"platform"})
}

func (plat Platform) VersionInfo() string {
	return plat.Version
}

// Status prints the status output specific to yb-platform.
func (plat Platform) Status() {

	name := "yb-platform"
	port := getYamlPathData(".platform.containerExposedPort")

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
		args := []string{"-c", "pgrep -f yb-platform"}
		out0, _ := ExecuteBashCommand(command, args)

		if strings.TrimSuffix(string(out0), "\n") != "" {
			runningStatus = "active"
		} else {
			runningStatus = "inactive"
		}
	}

	systemdLoc := "N/A"

	if hasSudoAccess() {

		systemdLoc = plat.SystemdFileLocation
	}

	outString := name + "\t" + plat.Version + "\t" + port +
		"\t" + plat.ConfFileLocation + "\t" + systemdLoc +
		"\t" + runningStatus + "\t"

	fmt.Fprintln(statusOutput, outString)

}

func configureConfHTTPS() {

	generateCertGolang()

	os.Chmod("key.pem", os.ModePerm)
	os.Chmod("cert.pem", os.ModePerm)

	os.MkdirAll(INSTALL_ROOT+"/yb-platform/certs", os.ModePerm)
	LogDebug(INSTALL_ROOT + "/yb-platform/certs directory successfully created.")

	keyStorePassword := getYamlPathData(".platform.keyStorePassword")

	ExecuteBashCommand("bash", []string{"-c", "./pemtokeystore-linux-amd64 -keystore server.ks " +
		"-keystore-password " + keyStorePassword + " -cert-file myserver=cert.pem " +
		"-key-file myserver=key.pem"})

	ExecuteBashCommand("bash", []string{"-c", "cp " + "server.ks" + " " + INSTALL_ROOT + "/yb-platform/certs"})

	if hasSudoAccess() {

		ExecuteBashCommand("chown", []string{"yugabyte:yugabyte", INSTALL_ROOT + "/yb-platform/certs"})
		ExecuteBashCommand("chown", []string{"yugabyte:yugabyte", INSTALL_ROOT + "/yb-platform/certs/server.ks"})

	}
}

func (plat Platform) CreateCronJob() {
	containerExposedPort := getYamlPathData(".platform.containerExposedPort")
	scriptPath := INSTALL_VERSION_DIR + "/crontabScripts/manage" + plat.Name + "NonRoot.sh"
	ExecuteBashCommand("bash", []string{"-c",
		"(crontab -l 2>/dev/null; echo \"@reboot " + scriptPath + " " + INSTALL_VERSION_DIR + " " +
			containerExposedPort + "\") | sort - | uniq - | crontab - "})
}
