/*
 * Copyright (c) YugaByte, Inc.
 */

package common

import (
	"fmt"
	"os"
	"path/filepath"
	"strings"

	"github.com/fluxcd/pkg/tar"
	"github.com/spf13/viper"

	log "github.com/yugabyte/yugabyte-db/managed/yba-installer/logging"
)

// Install performs the installation procedures common to
// all services.
func Install(version string) {
	log.Info("Starting Common install")
	// Hidden file written on first install (.installCompleted) at the end of the install,
	// if the file already exists then it means that an installation has already taken place,
	// and that future installs are prohibited.
	if _, err := os.Stat(InstalledFile); err == nil {
		log.Fatal("Install of YBA already completed, cannot perform reinstall without clean.")
	}

	// Change into the dir we are in so that we can specify paths relative to ourselves
	// TODO(minor): probably not a good idea in the long run
	os.Chdir(GetBinaryDir())

	MarkInstallStart()
	createYugabyteUser()
	createInstallDirs()
	copyBits(version)
	extractPlatformSupportPackageAndYugabundle(version)
	renameThirdPartyDependencies()
	setupJDK()
	setJDKEnvironmentVariable()
	log.Info("Finishing Common install")
}

// MarkInstallStart creates the marker file we use to show an in-progress install.
func MarkInstallStart() {
	// .installStarted written at the beginning of Installations, and renamed to
	// .installCompleted at the end of the install. That way, if an install fails midway,
	// the operations can be tried again in an idempotent manner. We also write the mode
	// of install to this file so that we can disallow installs between types (root ->
	// non-root and non-root -> root both prohibited).
	var data []byte
	if HasSudoAccess() {
		data = []byte("root\n")
	} else {
		data = []byte("non-root\n")
	}
	if err := os.WriteFile(installingFile, data, 0666); err != nil {
		log.Fatal("failed to mark instal as in progress: " + err.Error())
	}
}

func PostInstall() {
	// Symlink at /usr/local/bin/yba-ctl -> /opt/yba-ctl/yba-ctl -> actual yba-ctl
	if HasSudoAccess() {
		CreateSymlink(GetInstallVersionDir(), filepath.Dir(InputFile), goBinaryName)
		CreateSymlink(filepath.Dir(InputFile), "/usr/local/bin", goBinaryName)
	}

	MarkInstallComplete()
}

// MarkInstallComplete moves the .installing file to .installed to indicate install success.
func MarkInstallComplete() {
	MoveFileGolang(installingFile, InstalledFile)
}

func createInstallDirs() {
	createDirs := []string{
		GetInstallOne(),
		GetInstallTwo(),
		filepath.Join(GetBaseInstall(), "data"),
		filepath.Join(GetBaseInstall(), "data/logs"),
		GetInstallVersionDir(),
		filepath.Join(GetInstallVersionDir(), CronDir),
		filepath.Join(GetInstallVersionDir(), ConfigDir),
	}

	for _, dir := range createDirs {
		if err := os.MkdirAll(dir, os.ModePerm); err != nil {
			log.Fatal(fmt.Sprintf("failed creating directory %s: %s", dir, err.Error()))
		}
		err := Chown(dir, viper.GetString("service_username"), viper.GetString("service_username"), true)
		if err != nil {
			log.Fatal("failed to change ownership of " + dir + " to " +
				viper.GetString("service_username") + ": " + err.Error())
		}
	}

	// Mark our active directory
	if err := CreateInstallMarker(); err != nil {
		log.Fatal("could not create active install marker file: " + err.Error())
	}

	// Remove the symlink if one exists
	SetActiveInstallSymlink()
}

func createUpgradeDirs() {
	createDirs := []string{
		GetInstallVersionDir(),
		filepath.Join(GetInstallVersionDir(), CronDir),
		filepath.Join(GetInstallVersionDir(), ConfigDir),
	}

	for _, dir := range createDirs {
		if err := os.MkdirAll(dir, os.ModePerm); err != nil {
			log.Fatal(fmt.Sprintf("failed creating directory %s: %s", dir, err.Error()))
		}
		err := Chown(dir, viper.GetString("service_username"), viper.GetString("service_username"), true)
		if err != nil {
			log.Fatal("failed to change ownership of " + dir + " to " +
				viper.GetString("service_username") + ": " + err.Error())
		}
	}
}

// Copies over necessary files for all services from yba_installer_full to the GetInstallRoot()
func copyBits(vers string) {
	yugabundleBinary := "yugabundle-" + GetVersion() + "-centos-x86_64.tar.gz"
	neededFiles := []string{goBinaryName, versionMetadataJSON, "../" + yugabundleBinary,
		javaBinaryName, BundledPostgresName, pemToKeystoreConverter}

	for _, file := range neededFiles {
		_, err := ExecuteBashCommand("bash",
			[]string{"-c", "cp -p " + file + " " + GetInstallVersionDir()})
		if err != nil {
			log.Fatal("failed to copy " + file + ": " + err.Error())
		}
	}

	templateCpCmd := fmt.Sprintf("cp %s/* %s/%s",
		ConfigDir, GetInstallVersionDir(), ConfigDir)
	cronCpCmd := fmt.Sprintf("cp %s/* %s/%s",
		CronDir, GetInstallVersionDir(), CronDir)

	if _, err := ExecuteBashCommand("bash", []string{"-c", templateCpCmd}); err != nil {
		log.Fatal("failed to copy config files: " + err.Error())
	}
	if _, err := ExecuteBashCommand("bash", []string{"-c", cronCpCmd}); err != nil {
		log.Fatal("failed to copy cron scripts: " + err.Error())
	}
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

	os.RemoveAll(GetInstallVersionDir())

	// Remove the hidden marker file
	os.RemoveAll(InstalledFile)
}

// Upgrade performs the upgrade procedures common to all services.
func Upgrade(version string) {
	log.Info("Starting common upgrade")
	dm.CleanAltInstall()
	createUpgradeDirs()
	copyBits(version)
	extractPlatformSupportPackageAndYugabundle(version)
	renameThirdPartyDependencies()
	setupJDK()
	setJDKEnvironmentVariable()

	log.Info("Finishing common upgrade")
}

// SetActiveInstallSymlink will create <installRoot>/active symlink
func SetActiveInstallSymlink() {
	// Remove the symlink if one exists
	if _, err := os.Stat(GetActiveSymlink()); err == nil {
		os.Remove(GetActiveSymlink())
	}
	if err := os.Symlink(GetInstallRoot(), GetActiveSymlink()); err != nil {
		log.Fatal("could not create active symlink " + err.Error())
	}
}

func setupJDK() {
	command1 := "bash"
	arg1 := []string{"-c", "tar -zvxf " + javaBinaryName + " -C " + GetInstallVersionDir()}
	ExecuteBashCommand(command1, arg1)
}

// When we start Yugaware, we point to the location of our bundled
// JDK via the JAVA_HOME environment variable, so that it knows to
// use the version we provide. Needed for non-root installs as we cannot
// set the environment variables in the systemd service file any longer.
func setJDKEnvironmentVariable() {
	javaExtractedFolderName, err := ExecuteBashCommand("bash",
		[]string{"-c", "tar -tf " + javaBinaryName + " | head -n 1"})
	if err != nil {
		log.Fatal("failed to setup JDK environment: " + err.Error())
	}

	javaExtractedFolderName = strings.TrimSuffix(strings.ReplaceAll(javaExtractedFolderName,
		" ", ""), "/")
	javaHome := GetInstallVersionDir() + javaExtractedFolderName
	os.Setenv("JAVA_HOME", javaHome)
}

func createYugabyteUser() {
	userName := viper.GetString("service_username")
	if userName != "yugabyte" {
		log.Debug("skipping creation of non-yugabyte user " + userName)
		return
	}

	command1 := "bash"
	arg1 := []string{"-c", "id -u " + userName}
	_, err := ExecuteBashCommand(command1, arg1)

	// We will get an error if the user doesn't exist, as the ID command will fail.
	if err == nil {
		log.Debug("User " + userName + " already exists, skipping user creation.")
		return
	}
	if HasSudoAccess() {
		command2 := "useradd"
		arg2 := []string{userName}
		ExecuteBashCommand(command2, arg2)
	} else {
		log.Fatal("Need sudo access to create yugabyte user.")
	}
}

func extractPlatformSupportPackageAndYugabundle(vers string) {
	os.RemoveAll(GetInstallVersionDir() + "packages")

	yugabundleBinary := GetInstallVersionDir() + "/yugabundle-" + vers + "-centos-x86_64.tar.gz"

	rExtract1, errExtract1 := os.Open(yugabundleBinary)
	if errExtract1 != nil {
		log.Fatal("Error in starting the File Extraction process. " + errExtract1.Error())
	}
	defer rExtract1.Close()

	err := tar.Untar(rExtract1, GetInstallVersionDir(), tar.WithMaxUntarSize(-1))
	if err != nil {
		log.Fatal(fmt.Sprintf("failed to extract file %s, error: %s", yugabundleBinary, err.Error()))
	}

	path1 := GetInstallVersionDir() + "/yugabyte-" + vers +
		"/yugabundle_support-" + vers + "-centos-x86_64.tar.gz"

	rExtract2, errExtract2 := os.Open(path1)
	if errExtract2 != nil {
		fmt.Println(errExtract2.Error())
		log.Fatal("Error in starting the File Extraction process. " + errExtract2.Error())
	}
	defer rExtract2.Close()

	err = tar.Untar(rExtract2, GetInstallVersionDir(), tar.WithMaxUntarSize(-1))
	if err != nil {
		log.Fatal(fmt.Sprintf("failed to extract file %s, error: %s", path1, err.Error()))
	}

	log.Debug(path1 + " successfully extracted.")

	MoveFileGolang(GetInstallVersionDir()+"/yugabyte-"+vers,
		GetInstallVersionDir()+"/packages/yugabyte-"+vers)

	if HasSudoAccess() {
		userName := viper.GetString("service_username")
		Chown(GetInstallRoot(), userName, userName, true)
	}

}

func renameThirdPartyDependencies() {

	//Remove any thirdparty directories if they already exist, so
	//that the install action is idempotent.
	os.RemoveAll(GetInstallVersionDir() + "/thirdparty")
	// TODO: make path a variable here
	rExtract, _ := os.Open(GetInstallVersionDir() + "/packages/thirdparty-deps.tar.gz")
	if err := tar.Untar(rExtract, GetInstallVersionDir(), tar.WithMaxUntarSize(-1)); err != nil {
		log.Fatal(fmt.Sprintf("failed to extract file %s, error: %s",
			GetInstallVersionDir()+"/packages/thirdparty-deps.tar.gz", err.Error()))
	}
	log.Debug(GetInstallVersionDir() + "/packages/thirdparty-deps.tar.gz successfully extracted.")
	MoveFileGolang(GetInstallVersionDir()+"/thirdparty", GetInstallVersionDir()+"/third-party")
	//TODO: There is an error here because InstallRoot + "/yb-platform/third-party" does not exist
	/*ExecuteBashCommand("bash",
	[]string{"-c", "cp -R " + GetInstallVersionDir() + "/third-party" + " " +
		GetInstallRoot() + "/yb-platform/third-party"})
	*/
}
