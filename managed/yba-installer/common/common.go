/*
 * Copyright (c) YugaByte, Inc.
 */

package common

import (
	"crypto/tls"
	"encoding/json"
	"errors"
	"fmt"
	"io/fs"
	"net/http"
	"os"
	"path/filepath"
	"strings"
	"time"

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
	if _, err := os.Stat(YbaInstalledMarker()); err == nil {
		log.Fatal("Install of YBA already completed, cannot perform reinstall without clean.")
	}

	// Change into the dir we are in so that we can specify paths relative to ourselves
	// TODO(minor): probably not a good idea in the long run
	os.Chdir(GetBinaryDir())

	// set default values for unspecified config values
	fixConfigValues()

	createYugabyteUser()

	createInstallDirs()
	copyBits(version)
	extractPlatformSupportPackageAndYugabundle(version)
	renameThirdPartyDependencies()

	setupJDK()
	setJDKEnvironmentVariable()
}

func createInstallDirs() {
	createDirs := []string{
		dm.WorkingDirectory(),
		filepath.Join(GetBaseInstall(), "data"),
		filepath.Join(GetBaseInstall(), "data/logs"),
		GetInstallerSoftwareDir(),
		filepath.Join(GetInstallerSoftwareDir(), CronDir),
		filepath.Join(GetInstallerSoftwareDir(), ConfigDir),
	}

	for _, dir := range createDirs {
		if err := MkdirAll(dir, os.ModePerm); err != nil {
			log.Fatal(fmt.Sprintf("failed creating directory %s: %s", dir, err.Error()))
		}
		// Only change ownership for root installs.
		if HasSudoAccess() {
			err := Chown(dir, viper.GetString("service_username"), viper.GetString("service_username"), true)
			if err != nil {
				log.Fatal("failed to change ownership of " + dir + " to " +
					viper.GetString("service_username") + ": " + err.Error())
			}
		}
	}

	// Remove the symlink if one exists
	SetActiveInstallSymlink()
}

func createUpgradeDirs() {
	createDirs := []string{
		GetInstallerSoftwareDir(),
		filepath.Join(GetInstallerSoftwareDir(), CronDir),
		filepath.Join(GetInstallerSoftwareDir(), ConfigDir),
	}

	for _, dir := range createDirs {
		if err := MkdirAll(dir, os.ModePerm); err != nil {
			log.Fatal(fmt.Sprintf("failed creating directory %s: %s", dir, err.Error()))
		}
		if HasSudoAccess() {
			err := Chown(dir, viper.GetString("service_username"), viper.GetString("service_username"),
				true)
			if err != nil {
				log.Fatal("failed to change ownership of " + dir + " to " +
					viper.GetString("service_username") + ": " + err.Error())
			}
		}
	}
}

// Copies over necessary files for all services from yba_installer_full to the GetSoftwareRoot()
func copyBits(vers string) {
	yugabundleBinary := "yugabundle-" + GetVersion() + "-centos-x86_64.tar.gz"
	neededFiles := []string{GoBinaryName, versionMetadataJSON, yugabundleBinary,
		GetJavaPackagePath(), GetPostgresPackagePath()}

	for _, file := range neededFiles {
		_, err := RunBash("bash",
			[]string{"-c", "cp -p " + file + " " + GetInstallerSoftwareDir()})
		if err != nil {
			log.Fatal("failed to copy " + file + ": " + err.Error())
		}
	}

	configDirPath := GetTemplatesDir()
	templateCpCmd := fmt.Sprintf("cp %s/* %s/%s",
		configDirPath, GetInstallerSoftwareDir(), ConfigDir)

	cronDirPath := GetCronDir()
	cronCpCmd := fmt.Sprintf("cp %s/* %s/%s",
		cronDirPath, GetInstallerSoftwareDir(), CronDir)

	if _, err := RunBash("bash", []string{"-c", templateCpCmd}); err != nil {
		log.Fatal("failed to copy config files: " + err.Error())
	}
	if _, err := RunBash("bash", []string{"-c", cronCpCmd}); err != nil {
		log.Fatal("failed to copy cron scripts: " + err.Error())
	}
}

// Uninstall performs the uninstallation procedures common to
// all services when executing a clean.
func Uninstall(serviceNames []string, removeData bool) {

	// Removed the InstallVersionDir if it exists if we are not performing an upgrade
	// (since we would essentially perform a fresh install).

	RemoveAll(GetSoftwareDir())

	if removeData {
		err := RemoveAll(GetBaseInstall())
		if err != nil {
			pe := err.(*fs.PathError)
			if !errors.Is(pe.Err, fs.ErrNotExist) {
				log.Info(fmt.Sprintf("Failed to delete yba installer data dir %s", GetYBAInstallerDataDir()))
			}
		}
	}

	// Remove yba-ctl
	RemoveAll(YbactlInstallDir())
	os.Remove("/usr/bin/" + GoBinaryName)
}

// Upgrade performs the upgrade procedures common to all services.
func Upgrade(version string) {

	// Change into the dir we are in so that we can specify paths relative to ourselves
	// TODO(minor): probably not a good idea in the long run
	os.Chdir(GetBinaryDir())

	createUpgradeDirs()
	copyBits(version)
	extractPlatformSupportPackageAndYugabundle(version)
	renameThirdPartyDependencies()
	setupJDK()
	setJDKEnvironmentVariable()

}

// PostUpgrade will update the active install symlink and remove old versions if needed
func PostUpgrade() {
	SetActiveInstallSymlink()
	PrunePastInstalls()
}

// SetActiveInstallSymlink will create <installRoot>/active symlink
func SetActiveInstallSymlink() {
	// Remove the symlink if one exists
	if _, err := os.Stat(GetActiveSymlink()); err == nil {
		os.Remove(GetActiveSymlink())
	}
	if err := os.Symlink(GetSoftwareRoot(), GetActiveSymlink()); err != nil {
		log.Fatal("could not create active symlink " + err.Error())
	}
}

func setupJDK() {
	command1 := "bash"
	arg1 := []string{"-c", "tar -zxf " + GetJavaPackagePath() + " -C " + GetInstallerSoftwareDir()}
	RunBash(command1, arg1)
}

// When we start Yugaware, we point to the location of our bundled
// JDK via the JAVA_HOME environment variable, so that it knows to
// use the version we provide. Needed for non-root installs as we cannot
// set the environment variables in the systemd service file any longer.
func setJDKEnvironmentVariable() {
	javaExtractedFolderName, err := RunBash("bash",
		[]string{"-c", "tar -tf " + GetJavaPackagePath() + " | head -n 1"})
	if err != nil {
		log.Fatal("failed to setup JDK environment: " + err.Error())
	}

	javaExtractedFolderName = strings.TrimSuffix(strings.ReplaceAll(javaExtractedFolderName,
		" ", ""), "/")
	javaHome := GetInstallerSoftwareDir() + javaExtractedFolderName
	os.Setenv("JAVA_HOME", javaHome)
}

func createYugabyteUser() {
	userName := viper.GetString("service_username")
	if userName != DefaultServiceUser {
		log.Debug("skipping creation of non-yugabyte user " + userName)
		return
	}

	command1 := "bash"
	arg1 := []string{"-c", "id -u " + userName}
	_, err := RunBash(command1, arg1)

	// We will get an error if the user doesn't exist, as the ID command will fail.
	if err == nil {
		log.Debug("User " + userName + " already exists, skipping user creation.")
		return
	}
	if HasSudoAccess() {
		command2 := "useradd"
		arg2 := []string{userName}
		RunBash(command2, arg2)
	} else {
		log.Fatal("Need sudo access to create yugabyte user.")
	}
}

func extractPlatformSupportPackageAndYugabundle(vers string) {

	log.Info("Extracting yugabundle package.")

	RemoveAll(GetInstallerSoftwareDir() + "packages")

	yugabundleBinary := GetInstallerSoftwareDir() + "/yugabundle-" + vers + "-centos-x86_64.tar.gz"

	rExtract1, errExtract1 := os.Open(yugabundleBinary)
	if errExtract1 != nil {
		log.Fatal("Error in starting the File Extraction process. " + errExtract1.Error())
	}
	defer rExtract1.Close()

	log.Debug(fmt.Sprintf("Extracting %s", yugabundleBinary))
	err := tar.Untar(rExtract1, GetInstallerSoftwareDir(), tar.WithMaxUntarSize(-1))
	if err != nil {
		log.Fatal(fmt.Sprintf("failed to extract file %s, error: %s", yugabundleBinary, err.Error()))
	}
	log.Debug(fmt.Sprintf("Completed extracting %s", yugabundleBinary))

	path1 := GetInstallerSoftwareDir() + "/yugabyte-" + vers +
		"/yugabundle_support-" + vers + "-centos-x86_64.tar.gz"

	rExtract2, errExtract2 := os.Open(path1)
	if errExtract2 != nil {
		fmt.Println(errExtract2.Error())
		log.Fatal("Error in starting the File Extraction process. " + errExtract2.Error())
	}
	defer rExtract2.Close()

	log.Debug(fmt.Sprintf("Extracting %s", path1))
	err = tar.Untar(rExtract2, GetInstallerSoftwareDir(), tar.WithMaxUntarSize(-1))
	if err != nil {
		log.Fatal(fmt.Sprintf("failed to extract file %s, error: %s", path1, err.Error()))
	}
	log.Debug(fmt.Sprintf("Completed extracting %s", path1))

	RenameOrFail(GetInstallerSoftwareDir()+"/yugabyte-"+vers,
		GetInstallerSoftwareDir()+"/packages/yugabyte-"+vers)

	if HasSudoAccess() {
		userName := viper.GetString("service_username")
		Chown(GetSoftwareRoot(), userName, userName, true)
	}

}

func renameThirdPartyDependencies() {

	log.Info("Extracting third-party dependencies.")

	//Remove any thirdparty directories if they already exist, so
	//that the install action is idempotent.
	RemoveAll(GetInstallerSoftwareDir() + "/thirdparty")

	path := GetInstallerSoftwareDir() + "/packages/thirdparty-deps.tar.gz"
	rExtract, _ := os.Open(path)
	log.Debug("Extracting archive " + path)
	if err := tar.Untar(rExtract, GetInstallerSoftwareDir(), tar.WithMaxUntarSize(-1)); err != nil {
		log.Fatal(fmt.Sprintf("failed to extract file %s, error: %s",
			path, err.Error()))
	}

	log.Debug(fmt.Sprintf("Completed extracting archive at %s to %s", path, GetInstallerSoftwareDir()))
	RenameOrFail(GetInstallerSoftwareDir()+"/thirdparty", GetInstallerSoftwareDir()+"/third-party")
	//TODO: There is an error here because InstallRoot + "/yb-platform/third-party" does not exist
	/*RunBash("bash",
	[]string{"-c", "cp -R " + GetInstallerSoftwareDir() + "/third-party" + " " +
		GetSoftwareRoot() + "/yb-platform/third-party"})
	*/
}

func fixConfigValues() {

	if len(viper.GetString("service_username")) == 0 {
		log.Info(fmt.Sprintf("Systemd services will be run as user %s", DefaultServiceUser))
		setYamlValue(InputFile(), "service_username", DefaultServiceUser)
	}

	if len(viper.GetString("platform.appSecret")) == 0 {
		log.Debug("Generating default app secret for platform")
		setYamlValue(InputFile(), "platform.appSecret", GenerateRandomStringURLSafe(64))
		InitViper()
	}

	if len(viper.GetString("platform.keyStorePassword")) == 0 {
		log.Debug("Generating default app secret for platform")
		setYamlValue(InputFile(), "platform.keyStorePassword", GenerateRandomStringURLSafe(32))
		InitViper()
	}

	if len(viper.GetString("host")) == 0 {
		host := GuessPrimaryIP()
		log.Info("Guessing primary IP of host to be " + host)
		setYamlValue(InputFile(), "host", host)
		InitViper()
	}

	if len(viper.GetString("server_cert_path")) == 0 {
		log.Info("Generating self-signed server certificates")
		serverCertPath, serverKeyPath := generateSelfSignedCerts()
		setYamlValue(InputFile(), "server_cert_path", serverCertPath)
		setYamlValue(InputFile(), "server_key_path", serverKeyPath)
		InitViper()
	}

}

func generateSelfSignedCerts() (string, string) {
	certsDir := GetSelfSignedCertsDir()
	serverCertPath := filepath.Join(certsDir, ServerCertPath)
	serverKeyPath := filepath.Join(certsDir, ServerKeyPath)

	caCertPath := filepath.Join(certsDir, "ca_cert.pem")
	caKeyPath := filepath.Join(certsDir, "ca_key.pem")

	err := MkdirAll(certsDir, os.ModePerm)
	if err != nil && !os.IsExist(err) {
		log.Fatal(fmt.Sprintf("Unable to create dir %s", certsDir))
	}
	log.Debug("Created dir " + certsDir)

	generateSelfSignedServerCert(
		serverCertPath,
		serverKeyPath,
		caCertPath,
		caKeyPath,
		viper.GetString("host"),
	)

	return serverCertPath, serverKeyPath

}

func WaitForYBAReady() {
	log.Info("Waiting for YBA ready.")

	// Needed to access https URL without x509: certificate signed by unknown authority error
	http.DefaultTransport.(*http.Transport).TLSClientConfig = &tls.Config{InsecureSkipVerify: true}

	url := fmt.Sprintf("https://%s:%s/api/v1/app_version",
		viper.GetString("host"),
		viper.GetString("platform.port"))

	var resp *http.Response
	var err error
	// Check YBA version every 10 seconds for 2 minutes
	for i := 0; i < 12; i++ {
		resp, err = http.Get(url)
		if err != nil {
			log.Info(fmt.Sprintf("YBA at %s not ready. Checking again in 10 seconds.", url))
			time.Sleep(10 * time.Second)
		} else {
			break
		}
	}

	if resp != nil {
		defer resp.Body.Close()
	}

	// Validate version in prod
	if os.Getenv("YBA_MODE") != "dev" {
		if err == nil {
			var result map[string]string
			json.NewDecoder(resp.Body).Decode(&result)
			if result["version"] != GetVersion() {
				log.Fatal(fmt.Sprintf("Running YBA version %s does not match expected version %s",
					result["version"], GetVersion()))
			}
		} else {
			log.Fatal(fmt.Sprintf("Error waiting for YBA ready: %s", err.Error()))
		}
	}

}
