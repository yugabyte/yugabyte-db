/*
 * Copyright (c) YugaByte, Inc.
 */

package common

import (
	"fmt"
	"os"
	"strings"

	"github.com/fluxcd/pkg/tar"

	log "github.com/yugabyte/yugabyte-db/managed/yba-installer/logging"
)

// SetUpPrereqs performs the setup operations common to
// all services.
func SetUpPrereqs(version string) {
	log.Info("You are on version " + version +
		" of YBA Installer.")
}

// Install performs the installation procedures common to
// all services.
func Install(version string) {

	// Hidden file written on first install (.installCompleted) at the end of the install,
	// if the file already exists then it means that an installation has already taken place,
	// and that future installs are prohibited.
	if _, err := os.Stat(InstallRoot + "/.installCompleted"); err == nil {
		log.Fatal("Install of YBA already completed, cannot perform reinstall without clean.")
	}
	SetUpPrereqs(version)
	copyBits(version)
	// installPrerequisites()
	createYugabyteUser()
	extractPlatformSupportPackageAndYugabundle(version)
	renameThirdPartyDependencies()
	setupJDK()
	setJDKEnvironmentVariable()

}

// Copies over necessary files for all services from yba_installer_full to the InstallRoot
func copyBits(vers string) {

	// We make the install directory only if it doesn't exist. In pre-flight checks,
	// we look to see that the InstallRoot directory has free space and is writeable.
	dataDir := InstallRoot + "/data/logs"
	err := os.MkdirAll(dataDir, os.ModePerm)
	if err != nil && !os.IsExist(err) {
		log.Fatal(fmt.Sprintf("Making path %s failed, error %s", dataDir, err.Error()))
	}

	// .installStarted written at the beginning of Installations, and renamed to
	// .installCompleted at the end of the install. That way, if an install fails midway,
	// the operations can be tried again in an idempotent manner. We also write the mode
	// of install to this file so that we can disallow installs between types (root ->
	// non-root and non-root -> root both prohibited).

	if HasSudoAccess() {

		os.WriteFile(InstallRoot+"/.installStarted", []byte("root"), 0666)

	} else {

		os.WriteFile(InstallRoot+"/.installStarted", []byte("non-root"), 0666)

	}

	os.MkdirAll(InstallVersionDir, os.ModePerm)

	log.Debug(InstallRoot + " directory successfully created.")

	neededFiles := []string{goBinaryName, InputFile, versionMetadataJSON, "../" + yugabundleBinary,
		javaBinaryName, BundledPostgresName, pemToKeystoreConverter}

	for _, file := range neededFiles {

		ExecuteBashCommand("bash", []string{"-c", "cp -p " + file + " " + InstallVersionDir})

	}

	os.MkdirAll(fmt.Sprintf("%s/%s", InstallVersionDir, ConfigDir), os.ModePerm)
	os.MkdirAll(fmt.Sprintf("%s/%s", InstallVersionDir, CronDir), os.ModePerm)
	templateCpCmd := fmt.Sprintf("cp %s/* %s/%s",
		ConfigDir, InstallVersionDir, ConfigDir)
	cronCpCmd := fmt.Sprintf("cp %s/* %s/%s",
		CronDir, InstallVersionDir, CronDir)
	ExecuteBashCommand("bash", []string{"-c", templateCpCmd})
	ExecuteBashCommand("bash", []string{"-c", cronCpCmd})

	os.Chdir(InstallVersionDir)
}

// Uninstall performs the uninstallation procedures common to
// all services when executing a clean.
func Uninstall() {

	// 1) Stop all running service processes
	// 2) Delete service files (in root mode)/Stop cron/cleanup crontab in NonRoot
	// 3) Delete service directories
	// 4) Delete data dir in force mode (warn/ask for confirmation)

	service0 := "yb-platform"
	service1 := "prometheus"
	service2 := "postgres"
	services := []string{service0, service1, service2}

	if HasSudoAccess() {

		command := "service"

		for index := range services {
			commandCheck0 := "bash"
			subCheck0 := Systemctl + " list-unit-files --type service | grep -w " + services[index]
			argCheck0 := []string{"-c", subCheck0}
			out0, _ := ExecuteBashCommand(commandCheck0, argCheck0)
			if strings.TrimSuffix(string(out0), "\n") != "" {
				argStop := []string{services[index], "stop"}
				ExecuteBashCommand(command, argStop)
			}
		}
	} else {

		for index := range services {
			commandCheck0 := "bash"
			argCheck0 := []string{"-c", "pgrep -f " + services[index] + " | head -1"}
			out0, _ := ExecuteBashCommand(commandCheck0, argCheck0)
			// Need to stop the binary if it is running, can just do kill -9 PID (will work as the
			// process itself was started by a non-root user.)
			if strings.TrimSuffix(string(out0), "\n") != "" {
				pid := strings.TrimSuffix(string(out0), "\n")
				argStop := []string{"-c", "kill -9 " + pid}
				ExecuteBashCommand(commandCheck0, argStop)
			}
		}
	}

	// Removed the InstallVersionDir if it exists if we are not performing an upgrade
	// (since we would essentially perform a fresh install).

	os.RemoveAll(InstallVersionDir)

	// Remove the hidden marker file so that we are able to perform fresh installs of YBA,
	// with the retained data directories.
	os.RemoveAll(InstallRoot + "/.installCompleted")
}

// Upgrade performs the upgrade procedures common to all services.
func Upgrade(version string, services []Component) {

	copyBits(version)
	extractPlatformSupportPackageAndYugabundle(version)
	renameThirdPartyDependencies()
	setupJDK()
	setJDKEnvironmentVariable()

}

func setupJDK() {

	command1 := "bash"
	arg1 := []string{"-c", "tar -zvxf " + javaBinaryName + " -C " + InstallVersionDir}

	ExecuteBashCommand(command1, arg1)

}

// When we start Yugaware, we point to the location of our bundled
// JDK via the JAVA_HOME environment variable, so that it knows to
// use the version we provide. Needed for non-root installs as we cannot
// set the environment variables in the systemd service file any longer.
func setJDKEnvironmentVariable() {

	javaExtractedFolderName, _ := ExecuteBashCommand("bash",
		[]string{"-c", "tar -tf " + javaBinaryName + " | head -n 1"})

	javaExtractedFolderName = strings.TrimSuffix(strings.ReplaceAll(javaExtractedFolderName,
		" ", ""), "/")

	javaHome := InstallVersionDir + javaExtractedFolderName

	os.Setenv("JAVA_HOME", javaHome)

}

func createYugabyteUser() {

	command1 := "bash"
	arg1 := []string{"-c", "id -u yugabyte"}
	_, err := ExecuteBashCommand(command1, arg1)

	if err != nil {
		if HasSudoAccess() {
			command2 := "useradd"
			arg2 := []string{"yugabyte"}
			ExecuteBashCommand(command2, arg2)
		}
	} else {
		log.Debug("User yugabyte already exists os install is non-root, skipping user creation.")
	}

}

func extractPlatformSupportPackageAndYugabundle(vers string) {

	if HasSudoAccess() {
		command0 := "su"
		arg0 := []string{"yugabyte"}
		ExecuteBashCommand(command0, arg0)
	}

	os.RemoveAll(InstallVersionDir + "packages")

	yugabundleBinary := InstallVersionDir + "/yugabundle-" + vers + "-centos-x86_64.tar.gz"

	rExtract1, errExtract1 := os.Open(yugabundleBinary)
	if errExtract1 != nil {
		log.Fatal("Error in starting the File Extraction process.")
	}
	defer rExtract1.Close()

	err := tar.Untar(rExtract1, InstallVersionDir, tar.WithMaxUntarSize(-1))
	if err != nil {
		log.Fatal(fmt.Sprintf("failed to extract file %s, error: %s", yugabundleBinary, err.Error()))
	}

	path1 := InstallVersionDir + "/yugabyte-" + vers +
		"/yugabundle_support-" + vers + "-centos-x86_64.tar.gz"

	rExtract2, errExtract2 := os.Open(path1)
	if errExtract2 != nil {
		fmt.Println(errExtract2.Error())
		log.Fatal("Error in starting the File Extraction process.")
	}
	defer rExtract2.Close()

	err = tar.Untar(rExtract2, InstallVersionDir, tar.WithMaxUntarSize(-1))
	if err != nil {
		log.Fatal(fmt.Sprintf("failed to extract file %s, error: %s", path1, err.Error()))
	}

	log.Debug(path1 + " successfully extracted.")

	MoveFileGolang(InstallVersionDir+"/yugabyte-"+vers,
		InstallVersionDir+"/packages/yugabyte-"+vers)

	if HasSudoAccess() {
		command3 := "chown"
		arg3 := []string{"yugabyte:yugabyte", "-R", "/opt/yugabyte"}
		ExecuteBashCommand(command3, arg3)
	}

}

func renameThirdPartyDependencies() {

	//Remove any thirdparty directories if they already exist, so
	//that the install action is idempotent.
	os.RemoveAll(InstallVersionDir + "/thirdparty")
	// TODO: make path a variable here
	rExtract, _ := os.Open(InstallVersionDir + "/packages/thirdparty-deps.tar.gz")
	if err := tar.Untar(rExtract, InstallVersionDir, tar.WithMaxUntarSize(-1)); err != nil {
		log.Fatal(fmt.Sprintf("failed to extract file %s, error: %s",
			InstallVersionDir+"/packages/thirdparty-deps.tar.gz", err.Error()))
	}
	log.Debug(InstallVersionDir + "/packages/thirdparty-deps.tar.gz successfully extracted.")
	MoveFileGolang(InstallVersionDir+"/thirdparty", InstallVersionDir+"/third-party")
	//TODO: There is an error here because InstallRoot + "/yb-platform/third-party" does not exist
	ExecuteBashCommand("bash",
		[]string{"-c", "cp -R " + InstallVersionDir + "/third-party" + " " +
			InstallRoot + "/yb-platform/third-party"})
}
