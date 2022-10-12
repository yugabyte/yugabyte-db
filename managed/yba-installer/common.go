/*
 * Copyright (c) YugaByte, Inc.
 */

package main

import (
	"fmt"
	"github.com/spf13/viper"
	"os"
	"regexp"
	"strconv"
	"strings"
)

// Common (general setup operations)
type Common struct {
	Name    string
	Version string
}

// GetVersion gets the version at execution time so that yba-installer
// installs the correct version of Yugabyte Anywhere.
func GetVersion() string {

	cwd, _ := os.Getwd()
	currentFolderPathList := strings.Split(cwd, "/")
	lenFolder := len(currentFolderPathList)
	currFolder := currentFolderPathList[lenFolder-1]

	versionInformation := strings.Split(currFolder, "-")

	// In case we are not executing in the install directory, return
	// the version present in versionMetadata.json.
	if len(versionInformation) < 3 {

		viper.SetConfigName("version_metadata.json")
		viper.SetConfigType("json")
		viper.AddConfigPath(".")
		err := viper.ReadInConfig()
		if err != nil {
			panic(err)
		}

		versionNumber := fmt.Sprint(viper.Get("version_number"))
		buildNumber := fmt.Sprint(viper.Get("build_number"))

		version := versionNumber + "-" + buildNumber

		return version

	}

	versionNumber := versionInformation[1]
	buildNumber := versionInformation[2]

	version := versionNumber + "-" + buildNumber

	return version
}

// Status prints out the header information for the main
// status command.
func (com Common) Status() {
	outString := "Name" + "\t" + "Version" + "\t" + "Port" + "\t" +
		"Config File Locations" + "\t" + "Systemd File Locations" +
		"\t" + "Running Status" + "\t"
	fmt.Fprintln(statusOutput, outString)
}

// SetUpPrereqs performs the setup operations common to
// all services.
func (com Common) SetUpPrereqs() {
	LogInfo("You are on version " + version +
		" of YBA Installer.")
	License()
}

// Install performs the installation procedures common to
// all services.
func (com Common) Install() {

	// Hidden file written on first install (.installCompleted) at the end of the install,
	// if the file already exists then it means that an installation has already taken place,
	// and that future installs are prohibited.
	if _, err := os.Stat(INSTALL_ROOT + "/.installCompleted"); err == nil {
		LogError("Install of YBA already completed, cannot perform reinstall without clean.")
	}

	copyBits(com.Version)
	//installPrerequisites()
	createYugabyteUser()
	GenerateTemplatedConfiguration()
	com.extractPlatformSupportPackageAndYugabundle(com.Version)
	com.renameThirdPartyDependencies()
	setupJDK()
	setJDKEnvironmentVariable()

}

func copyBits(vers string) {

	// We make the install directory only if it doesn't exist. In pre-flight checks,
	// we look to see that the INSTALL_ROOT directory has free space and is writeable.

	os.MkdirAll(INSTALL_ROOT, os.ModePerm)

	// .installStarted written at the beginning of Installations, and renamed to
	// .installCompleted at the end of the install. That way, if an install fails midway,
	// the operations can be tried again in an idempotent manner. We also write the mode
	// of install to this file so that we can disallow installs between types (root ->
	// non-root and non-root -> root both prohibited).

	if hasSudoAccess() {

		os.WriteFile(INSTALL_ROOT+"/.installStarted", []byte("root"), 0666)

	} else {

		os.WriteFile(INSTALL_ROOT+"/.installStarted", []byte("non-root"), 0666)

	}

	os.MkdirAll(INSTALL_VERSION_DIR, os.ModePerm)

	LogDebug(INSTALL_ROOT + " directory successfully created.")

	neededFiles := []string{goBinaryName, inputFile, versionMetadataJson, yugabundleBinary,
		javaBinaryName, bundledPostgresName, pemToKeystoreConverter}

	for _, file := range neededFiles {

		ExecuteBashCommand("bash", []string{"-c", "cp -p " + file + " " + INSTALL_VERSION_DIR})

	}

	ExecuteBashCommand("bash", []string{"-c", "cp -R configFiles " + INSTALL_VERSION_DIR + "/configFiles"})
	ExecuteBashCommand("bash", []string{"-c", "cp -R crontabScripts " + INSTALL_VERSION_DIR + "/crontabScripts"})

	os.Chdir(INSTALL_VERSION_DIR)
}

// Uninstall performs the uninstallation procedures common to
// all services when executing a clean.
func (com Common) Uninstall() {

	service0 := "yb-platform"
	service1 := "prometheus"
	service2 := "postgres"
	services := []string{service0, service1, service2}

	if hasSudoAccess() {

		command := "service"

		for index := range services {
			commandCheck0 := "bash"
			subCheck0 := SYSTEMCTL + " list-unit-files --type service | grep -w " + services[index]
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

	RemoveAllExceptDataVolumes([]string{"yb-platform", "prometheus", "postgres"})

	// Removed the INSTALL_VERSION_DIR if it exists if we are not performing an upgrade
	// (since we would essentially perform a fresh install).

	os.RemoveAll(INSTALL_VERSION_DIR)

	// Remove the hidden marker file so that we are able to perform fresh installs of YBA,
	// with the retained data directories.
	os.RemoveAll(INSTALL_ROOT + "/.installCompleted")
}

// RemoveAllExceptDataVolumes removes the install directory from the host's
// operating system, except for data volumes specified in the input config
// file.
func RemoveAllExceptDataVolumes(services []string) {

	dataVolumesList := []string{}

	for _, service := range services {
		dataVolumes := getYamlPathData(".dataVolumes." + service)
		dataVolumesList = append(dataVolumesList, service+": "+dataVolumes)
	}

	LogDebug("All directories will be deleted except the following: " +
		strings.Join(dataVolumesList, "|") + ".")
	//In case more base dirs get added in the future.
	rootDirs := []string{INSTALL_ROOT}
	for _, dir := range rootDirs {
		// Only remove all except data volumes if the base directories exist.
		if _, err := os.Stat(dir); err == nil {
			for _, service := range services {

				baseDir := dir + "/" + service
				splitBaseDir := strings.Split(baseDir, "/")
				baseDirOneUp := strings.Join(splitBaseDir[0:len(splitBaseDir)-1], "/")
				dataVolumes := getYamlPathData(".dataVolumes." + service)
				if strings.ReplaceAll(strings.TrimSuffix(dataVolumes, "\n"), " ", "") != "" {
					splitDataVolumes := strings.Split(dataVolumes, ",")
					for _, volume := range splitDataVolumes {
						volume = strings.ReplaceAll(volume, " ", "")
						volume = baseDir + "/" + volume
						if _, err := os.Stat(volume); err == nil {
							if strings.Contains(volume, baseDir) {
								volumeMoved := strings.ReplaceAll(volume, baseDir, baseDirOneUp)
								MoveFileGolang(volume, volumeMoved)
							}
						}
					}
				}
				os.RemoveAll(baseDir)
				os.MkdirAll(baseDir, os.ModePerm)
				if strings.ReplaceAll(strings.TrimSuffix(dataVolumes, "\n"), " ", "") != "" {
					splitDataVolumes := strings.Split(dataVolumes, ",")
					for _, volume := range splitDataVolumes {
						volume = strings.ReplaceAll(volume, " ", "")
						volume = baseDir + "/" + volume
						if strings.Contains(volume, baseDir) {
							volumeMoved := strings.ReplaceAll(volume, baseDir, baseDirOneUp)
							if _, err := os.Stat(volumeMoved); err == nil {
								MoveFileGolang(volumeMoved, volume)
							}
						}
					}
				}
			}
		}
	}
}

// Upgrade performs the upgrade procedures common to all services.
func (com Common) Upgrade() {

	RemoveAllExceptDataVolumes([]string{"yb-platform", "prometheus", "postgres"})
	copyBits(com.Version)
	GenerateTemplatedConfiguration()
	com.extractPlatformSupportPackageAndYugabundle(com.Version)
	com.renameThirdPartyDependencies()
	setupJDK()
	setJDKEnvironmentVariable()

}

func installPrerequisites() {
	var bringOwnPython, errPython = strconv.ParseBool(getYamlPathData(".python.bringOwn"))

	if errPython != nil {
		LogError("Please set python.BringOwn to either true or false.")
	}

	if hasSudoAccess() {

		//Check if a Python3 install already exists that is symlinked to
		//Python 3.6, 3.7, 3.8, or 3.9. Install otherwise.
		command := "bash"
		args := []string{"-c", "python3 --version"}
		output, _ := ExecuteBashCommand(command, args)

		outputTrimmed := strings.TrimSuffix(output, "\n")

		re := regexp.MustCompile(`Python 3.6|Python 3.7|Python 3.8|Python 3.9`)

		if !re.MatchString(outputTrimmed) {

			if !bringOwnPython {
				InstallOS([]string{"python3"})
			}

		}

	}

}

func setupJDK() {

	command1 := "bash"
	arg1 := []string{"-c", "tar -zvxf " + javaBinaryName + " -C " + INSTALL_VERSION_DIR}

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

	javaHome := INSTALL_VERSION_DIR + javaExtractedFolderName

	os.Setenv("JAVA_HOME", javaHome)

}

func createYugabyteUser() {

	command1 := "bash"
	arg1 := []string{"-c", "id -u yugabyte"}
	_, err := ExecuteBashCommand(command1, arg1)

	if err != nil {
		if hasSudoAccess() {
			command2 := "useradd"
			arg2 := []string{"yugabyte"}
			ExecuteBashCommand(command2, arg2)
		}
	} else {
		LogDebug("User yugabyte already exists os install is non-root, skipping user creation.")
	}

}

func (com Common) extractPlatformSupportPackageAndYugabundle(vers string) {

	if hasSudoAccess() {
		command0 := "su"
		arg0 := []string{"yugabyte"}
		ExecuteBashCommand(command0, arg0)
	}

	os.RemoveAll(INSTALL_VERSION_DIR + "packages")

	path0 := INSTALL_VERSION_DIR + "/yugabundle-" + vers + "-centos-x86_64.tar.gz"

	rExtract1, errExtract1 := os.Open(path0)
	if errExtract1 != nil {
		LogError("Error in starting the File Extraction process.")
	}

	Untar(rExtract1, INSTALL_VERSION_DIR)

	path1 := INSTALL_VERSION_DIR + "/yugabyte-" + vers +
		"/yugabundle_support-" + vers + "-centos-x86_64.tar.gz"

	rExtract2, errExtract2 := os.Open(path1)
	if errExtract2 != nil {
		LogError("Error in starting the File Extraction process.")
	}

	Untar(rExtract2, INSTALL_VERSION_DIR)

	LogDebug(path1 + " successfully extracted.")

	MoveFileGolang(INSTALL_VERSION_DIR+"/yugabyte-"+vers,
		INSTALL_VERSION_DIR+"/packages/yugabyte-"+vers)

	if hasSudoAccess() {
		command3 := "chown"
		arg3 := []string{"yugabyte:yugabyte", "-R", "/opt/yugabyte"}
		ExecuteBashCommand(command3, arg3)
	}

}

func (com Common) renameThirdPartyDependencies() {

	//Remove any thirdparty directories if they already exist, so
	//that the install action is idempotent.
	os.RemoveAll(INSTALL_VERSION_DIR + "/thirdparty")
	rExtract, _ := os.Open(INSTALL_VERSION_DIR + "/packages/thirdparty-deps.tar.gz")
	Untar(rExtract, INSTALL_VERSION_DIR)
	LogDebug(INSTALL_VERSION_DIR + "/packages/thirdparty-deps.tar.gz successfully extracted.")
	MoveFileGolang(INSTALL_VERSION_DIR+"/thirdparty", INSTALL_VERSION_DIR+"/third-party")
	ExecuteBashCommand("bash", []string{"-c",
		"cp -R " + INSTALL_VERSION_DIR + "/third-party" + " " + INSTALL_ROOT + "/yb-platform/third-party"})
}
