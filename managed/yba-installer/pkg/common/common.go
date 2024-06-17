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
	"path"
	"path/filepath"
	"strings"
	"time"

	"github.com/fluxcd/pkg/tar"
	"github.com/spf13/viper"

	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/common/shell"
	log "github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/logging"
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
	FixConfigValues()

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
		GetSoftwareRoot(),
		dm.WorkingDirectory(),
		filepath.Join(GetBaseInstall(), "data"),
		filepath.Join(GetBaseInstall(), "data/logs"),
		GetInstallerSoftwareDir(),
		GetBaseInstall(),
	}

	for _, dir := range createDirs {
		_, err := os.Stat(dir)
		if os.IsNotExist(err) {
			if err := MkdirAll(dir, DirMode); err != nil {
				log.Fatal(fmt.Sprintf("failed creating directory %s: %s", dir, err.Error()))
			}
		}
		// Only change ownership for root installs.
		if HasSudoAccess() {
			serviceuser := viper.GetString("service_username")
			err := Chown(dir, serviceuser, serviceuser, false)
			if err != nil {
				log.Fatal("failed to change ownership of " + dir + " to " +
					serviceuser + ": " + err.Error())
			}
		}
	}

	// Remove the symlink if one exists
	SetActiveInstallSymlink()
}

func createUpgradeDirs() {
	createDirs := []string{
		GetInstallerSoftwareDir(),
	}

	for _, dir := range createDirs {
		if err := MkdirAll(dir, DirMode); err != nil {
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
	yugabundleBinary := "yugabundle-" + vers + "-centos-x86_64.tar.gz"
	neededFiles := []string{GoBinaryName, VersionMetadataJSON, yugabundleBinary,
		GetJavaPackagePath(), GetPostgresPackagePath()}

	for _, file := range neededFiles {
		fp := AbsoluteBundlePath(file)
		if err := Copy(fp, GetInstallerSoftwareDir(), false, true); err != nil {
			log.Fatal("failed to copy " + fp + ": " + err.Error())
		}
	}

	configDest := path.Join(GetInstallerSoftwareDir(), ConfigDir)
	if _, err := os.Stat(configDest); errors.Is(err, os.ErrNotExist) {
		if err := Copy(GetTemplatesDir(), configDest, true, false); err != nil {
			log.Fatal("failed to copy config files: " + err.Error())
		}
	} else {
		log.Debug("skipping template file copy, already exists")
	}

	cronDest := path.Join(GetInstallerSoftwareDir(), CronDir)
	if _, err := os.Stat(cronDest); errors.Is(err, os.ErrNotExist) {
		if err := Copy(GetCronDir(), cronDest, true, false); err != nil {
			log.Fatal("failed to copy cron scripts: " + err.Error())
		}
	} else {
		log.Debug("skipping cron directory copy, already exists")
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
		// Remove yba-ctl only when data is removed so state is preserved
		err = RemoveAll(YbactlInstallDir())
		if err != nil {
			log.Warn(fmt.Sprintf("Failed to delete yba-ctl dir %s", YbactlInstallDir()))
		}
	}

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
	dirName, err := javaDirectoryName()
	if err != nil {
		log.Fatal("failed to untar jdk: " + err.Error())
	}
	_, err = os.Stat(filepath.Join(GetInstallerSoftwareDir(), dirName))
	if errors.Is(err, os.ErrNotExist) {
		out := shell.Run("tar", "-zxf", GetJavaPackagePath(), "-C", GetInstallerSoftwareDir())
		if !out.SucceededOrLog() {
			log.Fatal("failed to setup JDK: " + out.Error.Error())
		}
	} else {
		log.Debug("jdk already extracted")
	}
}

// When we start Yugaware, we point to the location of our bundled
// JDK via the JAVA_HOME environment variable, so that it knows to
// use the version we provide. Needed for non-root installs as we cannot
// set the environment variables in the systemd service file any longer.
func setJDKEnvironmentVariable() {
	tarArgs := []string{
		"-tf", GetJavaPackagePath(),
		"|",
		"head", "-n", "1",
	}
	out := shell.RunShell("tar", tarArgs...)
	out.LogDebug()
	if !out.SucceededOrLog() {
		log.Fatal("failed to setup JDK environment: " + out.Error.Error())
	}

	javaExtractedFolderName, err := javaDirectoryName()
	if err != nil {
		log.Fatal("failed to setup JDK Environment: " + err.Error())
	}
	javaHome := GetInstallerSoftwareDir() + javaExtractedFolderName
	os.Setenv("JAVA_HOME", javaHome)
}

func javaDirectoryName() (string, error) {
	tarArgs := []string{
		"-tf", GetJavaPackagePath(),
		"|",
		"head", "-n", "1",
	}
	out := shell.RunShell("tar", tarArgs...)
	out.LogDebug()
	if !out.SucceededOrLog() {
		return "", fmt.Errorf("could not get java folder name: %w", out.Error)
	}

	javaExtractedFolderName := strings.TrimSuffix(
		strings.ReplaceAll(out.StdoutString(), " ", ""),
		"/")
	return javaExtractedFolderName, nil
}

func createYugabyteUser() {
	userName := viper.GetString("service_username")
	if userName != DefaultServiceUser {
		log.Debug("skipping creation of non-yugabyte user " + userName)
		return
	}

	// Check if the user exists. id command will have non 0 exit status if the user does not exist.
	if out := shell.Run("id", "-u", userName); out.Succeeded() {
		log.Debug("User " + userName + " already exists, skipping user creation.")
		return
	}

	if HasSudoAccess() {
		if out := shell.Run("useradd", "-m", userName, "-U"); !out.Succeeded() {
			log.Fatal("failed to create user " + userName + ": " + out.Error.Error())
		}
	} else {
		log.Fatal("Need sudo access to create yugabyte user.")
	}
}

func extractPlatformSupportPackageAndYugabundle(vers string) {

	log.Info("Extracting yugabundle package.")

	if err := RemoveAll(filepath.Join(GetInstallerSoftwareDir(), "packages")); err != nil {
		log.Fatal("failed to remove old packages: " + err.Error())
	}

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
	if err := RemoveAll(GetInstallerSoftwareDir() + "/thirdparty"); err != nil {
		log.Fatal("failed to clean thirdparty directory: " + err.Error())
	}
	if err := RemoveAll(GetInstallerSoftwareDir() + "/third-party"); err != nil {
		log.Fatal("failed to clean thirdparty directory: " + err.Error())
	}

	path := GetInstallerSoftwareDir() + "/packages/thirdparty-deps.tar.gz"
	rExtract, _ := os.Open(path)
	log.Debug("Extracting archive " + path)
	if err := tar.Untar(rExtract, GetInstallerSoftwareDir(), tar.WithMaxUntarSize(-1)); err != nil {
		log.Fatal(fmt.Sprintf("failed to extract file %s, error: %s",
			path, err.Error()))
	}

	log.Debug(fmt.Sprintf("Completed extracting archive at %s to %s", path, GetInstallerSoftwareDir()))
	RenameOrFail(GetInstallerSoftwareDir()+"/thirdparty", GetInstallerSoftwareDir()+"/third-party")
}

// FixConfigValues sets any mandatory config defaults not set by user (generally passwords)
func FixConfigValues() {

	if len(viper.GetString("service_username")) == 0 {
		log.Info(fmt.Sprintf("Systemd services will be run as user %s", DefaultServiceUser))
		SetYamlValue(InputFile(), "service_username", DefaultServiceUser)
		InitViper()
	}

	if len(viper.GetString("platform.appSecret")) == 0 {
		log.Debug("Generating default app secret for platform")
		SetYamlValue(InputFile(), "platform.appSecret", GenerateRandomStringURLSafe(64))
		InitViper()
	}

	if len(viper.GetString("platform.keyStorePassword")) == 0 {
		log.Debug("Generating default app secret for platform")
		SetYamlValue(InputFile(), "platform.keyStorePassword", GenerateRandomStringURLSafe(32))
		InitViper()
	}

	if len(viper.GetString("host")) == 0 {
		host := GuessPrimaryIP()
		log.Info("Guessing primary IP of host to be " + host)
		SetYamlValue(InputFile(), "host", host)
		InitViper()
	}

	var serverCertPath, serverKeyPath string
	if len(viper.GetString("server_cert_path")) == 0 {
		log.Info("Generating self-signed server certificates")
		serverCertPath, serverKeyPath = GenerateSelfSignedCerts()
		SetYamlValue(InputFile(), "server_cert_path", serverCertPath)
		SetYamlValue(InputFile(), "server_key_path", serverKeyPath)
		InitViper()
	}

	if viper.GetBool("postgres.install.enabled") &&
		len(viper.GetString("postgres.install.password")) == 0 {
		log.Info("Generating default password for postgres")
		SetYamlValue(InputFile(), "postgres.install.password", GenerateRandomStringURLSafe(32))
		InitViper()
	}

	if viper.GetBool("prometheus.enableAuth") &&
		len(viper.GetString("prometheus.authPassword")) == 0 {
		log.Info("Generating default password for prometheus")
		SetYamlValue(InputFile(), "prometheus.authPassword", GenerateRandomStringURLSafe(32))
		InitViper()
	}

	if viper.GetBool("prometheus.enableHttps") {
		// Default to YBA certs
		SetYamlValue(InputFile(), "prometheus.httpsCertPath", viper.GetString("server_cert_path"))
		SetYamlValue(InputFile(), "prometheus.httpsKeyPath", viper.GetString("server_key_path"))
		InitViper()
	}

}

// GenerateSelfSignedCerts will create self signed certifacts and return their paths.
func GenerateSelfSignedCerts() (string, string) {
	certsDir := GetSelfSignedCertsDir()
	serverCertPath := filepath.Join(certsDir, ServerCertPath)
	serverKeyPath := filepath.Join(certsDir, ServerKeyPath)

	caCertPath := filepath.Join(certsDir, "ca_cert.pem")
	caKeyPath := filepath.Join(certsDir, "ca_key.pem")

	err := MkdirAll(certsDir, DirMode)
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

// RegenerateSelfSignedCerts will recreate the server cert and server key file with existing
// ca cert/key files.
// No directories will be created, this should already exist.
// Returns the path to the server cert and key.
func RegenerateSelfSignedCerts() (string, string) {
	certsDir := GetSelfSignedCertsDir()
	caCertPath := filepath.Join(certsDir, "ca_cert.pem")
	caKeyPath := filepath.Join(certsDir, "ca_key.pem")
	if _, err := os.Stat(caCertPath); errors.Is(err, os.ErrNotExist) {
		log.Info("no ca certs found, regenerating the entire cert chian")
		return GenerateSelfSignedCerts()
	}
	caCert, err := parseCertFromPem(caCertPath)
	if err != nil {
		log.Fatal("failed to parse certificate: " + err.Error())
	}
	caKey, err := parsePrivateKey(caKeyPath)
	if err != nil {
		log.Fatal("failed to parse private key: " + err.Error())
	}
	serverCertPath := filepath.Join(certsDir, ServerCertPath)
	serverKeyPath := filepath.Join(certsDir, ServerKeyPath)
	generateCert(serverCertPath, serverKeyPath, false /*isCA*/, serverCertTimeout,
		viper.GetString("host"), caCert, caKey)
	return serverCertPath, serverKeyPath
}

// WaitForYBAReady waits for a YBA to be running with specified version
func WaitForYBAReady(version string) {
	log.Info("Waiting for YBA ready.")

	// Needed to access https URL without x509: certificate signed by unknown authority error
	http.DefaultTransport.(*http.Transport).TLSClientConfig = &tls.Config{InsecureSkipVerify: true}

	hostnames := SplitInput(viper.GetString("host"))

	for _, host := range hostnames {

		url := fmt.Sprintf("https://%s:%s/api/v1/app_version",
			host,
			viper.GetString("platform.port"))

		var resp *http.Response
		var err error
		// Check YBA version every 10 seconds
		retriesCount := 20

		for i := 0; i < retriesCount; i++ {
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
				if result["version"] != version {
					log.Fatal(fmt.Sprintf("Running YBA version %s does not match expected version %s",
						result["version"], version))
				}
			} else {
				log.Fatal(fmt.Sprintf("Error waiting for YBA ready: %s", err.Error()))
			}
		}

	}

}
