/*
 * Copyright (c) YugaByte, Inc.
 */

package cmd

import (
	"fmt"
	"net"
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

// Component 3: Platform
type Platform struct {
	name                string
	SystemdFileLocation string
	ConfFileLocation    string
	templateFileName    string
	version             string
	DataDir             string
	cronScript          string
}

// NewPlatform creates a new YBA service struct.
func NewPlatform(installRoot, version string) Platform {
	return Platform{
		"yb-platform",
		common.SystemdDir + "/yb-platform.service",
		installRoot + "/yb-platform/conf/yb-platform.conf",
		"yba-installer-platform.yml",
		version,
		installRoot + "/data/yb-platform",
		fmt.Sprintf("%s/%s/managePlatform.sh", common.InstallVersionDir, common.CronDir)}
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
	return common.InstallVersionDir + "/packages/" + plat.packageFolder()
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
func (plat Platform) Install() {
	config.GenerateTemplate(plat)
	plat.createNecessaryDirectories()
	plat.createDevopsAndYugawareDirectories()
	plat.untarDevopsAndYugawarePackages()
	plat.copyYugabyteReleaseFile()
	plat.copyYbcPackages()
	plat.renameAndCreateSymlinks()
	configureConfHTTPS()

	//Create the platform.log file so that we can start platform as
	//a background process for non-root.
	common.Create(common.InstallRoot + "/yb-platform/yugaware/bin/platform.log")

	//Crontab based monitoring for non-root installs.
	if !common.HasSudoAccess() {
		plat.CreateCronJob()
	} else {
		// Allow yugabyte user to fully manage this installation (InstallRoot to be safe)
		userName := viper.GetString("service_username")
		common.Chown(common.InstallRoot, userName, userName, true)
	}
	// At the end of the installation, we rename .installStarted to .installCompleted, to signify the
	// install has finished succesfully.
	common.MoveFileGolang(common.InstallRoot+"/.installStarted",
		common.InstallRoot+"/.installCompleted")

	plat.Start()

}

func (plat Platform) createNecessaryDirectories() {

	os.MkdirAll(common.InstallRoot+"/yb-platform", os.ModePerm)
	os.MkdirAll(common.InstallRoot+"/data/yb-platform/releases/"+plat.version, os.ModePerm)
	os.MkdirAll(common.InstallRoot+"/data/yb-platform/ybc/release", os.ModePerm)
	os.MkdirAll(common.InstallRoot+"/data/yb-platform/ybc/releases", os.ModePerm)

}

func (plat Platform) createDevopsAndYugawareDirectories() {

	os.MkdirAll(plat.devopsDir(), os.ModePerm)
	os.MkdirAll(plat.yugawareDir(), os.ModePerm)

}

func (plat Platform) untarDevopsAndYugawarePackages() {

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

			if err := tar.Untar(rExtract, packageFolderPath+"/devops",
				tar.WithMaxUntarSize(-1)); err != nil {
				log.Fatal(fmt.Sprintf("failed to extract file %s, error: %s", devopsTgzPath, err.Error()))
			}

		} else if strings.Contains(f.Name(), "yugaware") && strings.Contains(f.Name(), "tar") {

			yugawareTgzName := f.Name()
			yugawareTgzPath := packageFolderPath + "/" + yugawareTgzName
			rExtract, errExtract := os.Open(yugawareTgzPath)
			if errExtract != nil {
				log.Fatal("Error in starting the File Extraction process.")
			}

			if err := tar.Untar(rExtract, packageFolderPath+"/yugaware",
				tar.WithMaxUntarSize(-1)); err != nil {
				log.Fatal(fmt.Sprintf("failed to extract file %s, error: %s", yugawareTgzPath, err.Error()))
			}

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
			common.CopyFileGolang(yugabyteTgzPath,
				common.InstallRoot+"/data/yb-platform/releases/"+plat.version+"/"+yugabyteTgzName)

		}
	}
}

func (plat Platform) copyYbcPackages() {
	packageFolderPath := common.InstallVersionDir + "/packages/yugabyte-" + plat.version
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
		common.CopyFileGolang(f, common.InstallRoot+"/data/yb-platform/ybc/release/"+fileName)
	}

}

func (plat Platform) renameAndCreateSymlinks() {

	common.CreateSymlink(plat.yugabyteDir(), common.InstallRoot+"/yb-platform", "yugaware")
	common.CreateSymlink(plat.yugabyteDir(), common.InstallRoot+"/yb-platform", "devops")

}

// Start the YBA platform service.
func (plat Platform) Start() {

	if common.HasSudoAccess() {

		common.ExecuteBashCommand(common.Systemctl,
			[]string{"daemon-reload"})
		common.ExecuteBashCommand(common.Systemctl,
			[]string{"enable", filepath.Base(plat.SystemdFileLocation)})
		common.ExecuteBashCommand(common.Systemctl,
			[]string{"start", filepath.Base(plat.SystemdFileLocation)})
		common.ExecuteBashCommand(common.Systemctl,
			[]string{"status", filepath.Base(plat.SystemdFileLocation)})

	} else {

		containerExposedPort := config.GetYamlPathData("platform.containerExposedPort")
		restartSeconds := config.GetYamlPathData("platform.restartSeconds")

		command1 := "bash"
		arg1 := []string{"-c", plat.cronScript + " " + common.InstallVersionDir + " " +
			containerExposedPort + " " + restartSeconds + " > /dev/null 2>&1 &"}

		common.ExecuteBashCommand(command1, arg1)

	}

}

// Stop the YBA platform service.
func (plat Platform) Stop() {

	if common.HasSudoAccess() {

		arg1 := []string{"stop", filepath.Base(plat.SystemdFileLocation)}
		common.ExecuteBashCommand(common.Systemctl, arg1)

	} else {

		// Delete the file used by the crontab bash script for monitoring.
		os.RemoveAll(common.InstallRoot + "/yb-platform/testfile")

		commandCheck0 := "bash"
		argCheck0 := []string{"-c", "pgrep -fl yb-platform"}
		out0, _ := common.ExecuteBashCommand(commandCheck0, argCheck0)

		// Need to stop the binary if it is running, can just do kill -9 PID (will work as the
		// process itself was started by a non-root user.)

		// Java check because pgrep will count the execution of yba-ctl as a process itself.
		if strings.TrimSuffix(string(out0), "\n") != "" {
			pids := strings.Split(string(out0), "\n")
			for _, pid := range pids {
				if strings.Contains(pid, "java") {
					argStop := []string{"-c", "kill -9 " + strings.TrimSuffix(pid, "\n")}
					common.ExecuteBashCommand(commandCheck0, argStop)
				}
			}
		}
	}
}

// Restart the YBA platform service.
func (plat Platform) Restart() {

	if common.HasSudoAccess() {

		arg1 := []string{"restart", "yb-platform.service"}
		common.ExecuteBashCommand(common.Systemctl, arg1)

	} else {

		plat.Stop()
		plat.Start()

	}

}

// Uninstall the YBA platform service and optionally clean out data.
func (plat Platform) Uninstall(removeData bool) {
	// Stop running platform service
	plat.Stop()

	// Clean up systemd file
	if common.HasSudoAccess() {
		err := os.Remove(plat.SystemdFileLocation)
		if err != nil {
			log.Info(fmt.Sprintf("Error %s removing systemd service %s.",
				err.Error(), plat.SystemdFileLocation))
		}
	}

	// Optionally remove data
	if removeData {
		err := os.RemoveAll(plat.DataDir)
		if err != nil {
			log.Info(fmt.Sprintf("Error %s removing data dir %s.", err.Error(), plat.DataDir))
		}

	}

}

// Status prints the status output specific to yb-platform.
func (plat Platform) Status() common.Status {
	status := common.Status{
		Service:   plat.Name(),
		Port:      viper.GetInt("platform.containerExposedPort"),
		Version:   plat.version,
		ConfigLoc: plat.ConfFileLocation,
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
		command := "bash"
		args := []string{"-c", "pgrep -f yb-platform"}
		out0, _ := common.ExecuteBashCommand(command, args)

		if strings.TrimSuffix(string(out0), "\n") != "" {
			status.Status = common.StatusRunning
		} else {
			status.Status = common.StatusStopped
		}
	}
	return status
}

func configureConfHTTPS() {

	generateCertGolang()

	os.Chmod("key.pem", os.ModePerm)
	os.Chmod("cert.pem", os.ModePerm)

	os.MkdirAll(common.InstallRoot+"/yb-platform/certs", os.ModePerm)
	log.Debug(common.InstallRoot + "/yb-platform/certs directory successfully created.")

	// Do not use viper because we might have to return the default.
	keyStorePassword := config.GetYamlPathData("platform.keyStorePassword")

	common.ExecuteBashCommand("bash",
		[]string{"-c", "./pemtokeystore-linux-amd64 -keystore server.ks " +
			"-keystore-password " + keyStorePassword +
			" -cert-file myserver=cert.pem " +
			"-key-file myserver=key.pem"})

	common.ExecuteBashCommand("bash",
		[]string{"-c", "cp " + "server.ks" + " " + common.InstallRoot + "/yb-platform/certs"})

	if common.HasSudoAccess() {
		userName := viper.GetString("service_username")
		common.Chown(common.InstallRoot+"/yb-platform/certs", userName, userName, true)

	}
}

// CreateCronJob creates the cron job for managing YBA platform with cron script in non-root.
func (plat Platform) CreateCronJob() {
	containerExposedPort := config.GetYamlPathData("platform.containerExposedPort")
	restartSeconds := config.GetYamlPathData("platform.restartSeconds")
	common.ExecuteBashCommand("bash", []string{"-c",
		"(crontab -l 2>/dev/null; echo \"@reboot " + plat.cronScript + " " + common.InstallVersionDir +
			" " + containerExposedPort + " " + restartSeconds + "\") | sort - | uniq - | crontab - "})
}

// GenerateCORSOrigin determines the IP address of the host to populate CORS origin field in conf.
func (plat Platform) GenerateCORSOrigin() string {
	conn, err := net.Dial("udp", "8.8.8.8:80")
	if err != nil {
		panic(err)
	}
	defer conn.Close()

	localAddr := conn.LocalAddr().(*net.UDPAddr)
	return "https:" + localAddr.IP.String()
}
