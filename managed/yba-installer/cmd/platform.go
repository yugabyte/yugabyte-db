/*
 * Copyright (c) YugaByte, Inc.
 */

package cmd

import (
	"fmt"
	"io/ioutil"
	"os"
	"path/filepath"
	"strings"

	"github.com/fluxcd/pkg/tar"
	"github.com/spf13/viper"
)

// Component 3: Platform
type Platform struct {
	Name                string
	SystemdFileLocation string
	ConfFileLocation    string
	templateFileName    string
	Version             string
	CorsOrigin          string
}

func NewPlatform(installRoot, version string) Platform {
	return Platform{
		"yb-platform",
		SYSTEMD_DIR + "/yb-platform.service",
		installRoot + "/yb-platform/conf/yb-platform.conf",
		"yba-installer-platform.yml",
		version,
		GenerateCORSOrigin()}
}

// Method of the Component
// Interface are implemented by
// the Platform struct and customizable
// for each specific service.

func (plat Platform) Install() {

	plat.createNecessaryDirectories()
	plat.createDevopsAndYugawareDirectories()
	plat.untarDevopsAndYugawarePackages()
	plat.copyYugabyteReleaseFile()
	plat.copyYbcPackages()
	plat.renameAndCreateSymlinks()
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
		Chown(INSTALL_ROOT, "yugabyte", "yugabyte", true)
	}

	// At the end of the installation, we rename .installStarted to .installCompleted, to signify the
	// install has finished succesfully.
	MoveFileGolang(INSTALL_ROOT+"/.installStarted", INSTALL_ROOT+"/.installCompleted")

}

func (plat Platform) createNecessaryDirectories() {

	os.MkdirAll(INSTALL_ROOT+"/yb-platform", os.ModePerm)
	os.MkdirAll(INSTALL_ROOT+"/data/yb-platform/releases/"+plat.Version, os.ModePerm)
	os.MkdirAll(INSTALL_ROOT+"/data/yb-platform/ybc/release", os.ModePerm)
	os.MkdirAll(INSTALL_ROOT+"/data/yb-platform/ybc/releases", os.ModePerm)

}

func (plat Platform) createDevopsAndYugawareDirectories() {

	os.MkdirAll(plat.getDevopsDir(), os.ModePerm)
	os.MkdirAll(INSTALL_VERSION_DIR+"/packages/"+plat.getPackageFolder()+"/yugaware", os.ModePerm)

}

func (plat Platform) untarDevopsAndYugawarePackages() {

	packageFolderPath := plat.getPackageFolderPath()

	files, err := ioutil.ReadDir(packageFolderPath)
	if err != nil {
		LogError("Error: " + err.Error() + ".")
	}

	for _, f := range files {
		if strings.Contains(f.Name(), "devops") && strings.Contains(f.Name(), "tar") {

			devopsTgzName := f.Name()
			devopsTgzPath := packageFolderPath + "/" + devopsTgzName
			rExtract, errExtract := os.Open(devopsTgzPath)
			if errExtract != nil {
				LogError("Error in starting the File Extraction process.")
			}

			if err := tar.Untar(rExtract, packageFolderPath+"/devops",
				tar.WithMaxUntarSize(-1)); err != nil {
				LogError(fmt.Sprintf("failed to extract file %s, error: %s", devopsTgzPath, err.Error()))
			}

		} else if strings.Contains(f.Name(), "yugaware") && strings.Contains(f.Name(), "tar") {

			yugawareTgzName := f.Name()
			yugawareTgzPath := packageFolderPath + "/" + yugawareTgzName
			rExtract, errExtract := os.Open(yugawareTgzPath)
			if errExtract != nil {
				LogError("Error in starting the File Extraction process.")
			}

			if err := tar.Untar(rExtract, packageFolderPath+"/yugaware",
				tar.WithMaxUntarSize(-1)); err != nil {
				LogError(fmt.Sprintf("failed to extract file %s, error: %s", yugawareTgzPath, err.Error()))
			}

		}
	}

}

func (plat Platform) copyYugabyteReleaseFile() {

	packageFolderPath := plat.getPackageFolderPath()

	files, err := ioutil.ReadDir(packageFolderPath)
	if err != nil {
		LogError("Error: " + err.Error() + ".")
	}

	for _, f := range files {
		if strings.Contains(f.Name(), "yugabyte") {

			yugabyteTgzName := f.Name()
			yugabyteTgzPath := packageFolderPath + "/" + yugabyteTgzName
			CopyFileGolang(yugabyteTgzPath,
				INSTALL_ROOT+"/data/yb-platform/releases/"+plat.Version+"/"+yugabyteTgzName)

		}
	}
}

func (plat Platform) copyYbcPackages() {
	packageFolderPath := INSTALL_VERSION_DIR + "/packages/yugabyte-" + plat.Version
	ybcPattern := packageFolderPath + "/**/ybc/ybc*.tar.gz"

	matches, err := filepath.Glob(ybcPattern)
	if err != nil {
		LogError(
			fmt.Sprintf("Could not find ybc components in %s. Failed with err %s",
				packageFolderPath, err.Error()))
	}

	for _, f := range matches {
		_, fileName := filepath.Split(f)
		// TODO: Check if file does not already exist?
		CopyFileGolang(f, INSTALL_ROOT+"/data/yb-platform/ybc/release/"+fileName)
	}

}

func (plat Platform) renameAndCreateSymlinks() {

	packageFolderPath := plat.getPackageFolderPath()

	yugawarePackagePath := packageFolderPath + "/yugaware"

	yugawareSymlink := INSTALL_ROOT + "/yb-platform/yugaware"

	devopsPackagePath := packageFolderPath + "/devops"
	devopsSymlink := INSTALL_ROOT + "/yb-platform/devops"

	if hasSudoAccess() {
		err := os.Symlink(yugawarePackagePath, yugawareSymlink)
		if err != nil {
			LogError(fmt.Sprintf("Error %s creating symlink %s to %s",
				err.Error(), yugawareSymlink, yugawarePackagePath))
		}
		err = os.Symlink(devopsPackagePath, devopsSymlink)
		if err != nil {
			LogError(fmt.Sprintf("Error %s creating symlink %s to %s",
				err.Error(), devopsSymlink, devopsPackagePath))
		}
	} else {
		LogError("Symlinking not implemented for non-root.")
	}

}

func (plat Platform) Start() {

	if hasSudoAccess() {

		ExecuteBashCommand(SYSTEMCTL, []string{"daemon-reload"})
		ExecuteBashCommand(SYSTEMCTL, []string{"enable", filepath.Base(plat.SystemdFileLocation)})
		ExecuteBashCommand(SYSTEMCTL, []string{"start", filepath.Base(plat.SystemdFileLocation)})
		ExecuteBashCommand(SYSTEMCTL, []string{"status", filepath.Base(plat.SystemdFileLocation)})

	} else {

		containerExposedPort := fmt.Sprintf("%d", viper.GetInt("platform.containerExposedPort"))
		restartSeconds := fmt.Sprintf("%d", viper.GetInt("platform.restartSeconds"))

		scriptPath := INSTALL_VERSION_DIR + "/crontabScripts/manage" + plat.Name + "NonRoot.sh"

		command1 := "bash"
		arg1 := []string{"-c", scriptPath + " " + INSTALL_VERSION_DIR + " " + containerExposedPort +
			" " + restartSeconds + " > /dev/null 2>&1 &"}

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

func (plat Platform) getSystemdFile() string {
	return plat.SystemdFileLocation
}

func (plat Platform) getConfFile() string {
	return plat.ConfFileLocation
}

func (plat Platform) getTemplateFile() string {
	return plat.templateFileName
}

func (plat Platform) getDevopsDir() string {
	return plat.getPackageFolderPath() + "/devops"
}

func (plat Platform) getPackageFolder() string {
	return "yugabyte-" + plat.Version
}

func (plat Platform) getPackageFolderPath() string {
	return INSTALL_VERSION_DIR + "/packages/" + plat.getPackageFolder()
}

func (plat Platform) getBackupScript() string {
	return plat.getDevopsDir() + "/bin/yb_platform_backup.sh"
}

// Per current cleanup.sh script.
func (plat Platform) Uninstall(removeData bool) {
	plat.Stop()
	RemoveAllExceptDataVolumes([]string{"platform"})
}

func (plat Platform) VersionInfo() string {
	return plat.Version
}

// Status prints the status output specific to yb-platform.
func (plat Platform) Status() {

	name := "yb-platform"
	port := fmt.Sprintf("%d", viper.GetInt("platform.externalPort"))

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

	keyStorePassword := viper.GetString("platform.keyStorePassword")

	ExecuteBashCommand("bash", []string{"-c", "./pemtokeystore-linux-amd64 -keystore server.ks " +
		"-keystore-password " + keyStorePassword + " -cert-file myserver=cert.pem " +
		"-key-file myserver=key.pem"})

	ExecuteBashCommand("bash",
		[]string{"-c", "cp " + "server.ks" + " " + INSTALL_ROOT + "/yb-platform/certs"})

	if hasSudoAccess() {

		ExecuteBashCommand(CHOWN, []string{"yugabyte:yugabyte", INSTALL_ROOT + "/yb-platform/certs"})
		ExecuteBashCommand(CHOWN,
			[]string{"yugabyte:yugabyte", INSTALL_ROOT + "/yb-platform/certs/server.ks"})

	}
}

func (plat Platform) CreateCronJob() {
	containerExposedPort := fmt.Sprintf("%d", viper.GetInt("platform.containerExposedPort"))
	restartSeconds := fmt.Sprintf("%d", viper.GetInt("platform.restartSeconds"))
	scriptPath := INSTALL_VERSION_DIR + "/crontabScripts/manage" + plat.Name + "NonRoot.sh"
	ExecuteBashCommand("bash", []string{"-c",
		"(crontab -l 2>/dev/null; echo \"@reboot " + scriptPath + " " + INSTALL_VERSION_DIR + " " +
			containerExposedPort + " " + restartSeconds + "\") | sort - | uniq - | crontab - "})
}
