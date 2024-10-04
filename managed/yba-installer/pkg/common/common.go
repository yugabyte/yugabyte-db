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
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/systemd"
)

// Install performs the installation procedures common to
// all services.
func Install(version string) error {
	log.Info("Starting Common install")

	// Change into the dir we are in so that we can specify paths relative to ourselves
	// TODO(minor): probably not a good idea in the long run
	if err := os.Chdir(GetBinaryDir()); err != nil {
		return err
	}

	// set default values for unspecified config values
	if err := FixConfigValues(); err != nil {
		return err
	}

	if err := createYugabyteUser(); err != nil {
		return err
	}

	// Set ownership of yba-ctl.yml and yba-ctl.log
	user := viper.GetString("service_username")
	if err := Chown(InputFile(), user, user, false); err != nil {
		return fmt.Errorf("could not set ownership of %s: %v", InputFile(), err)
	}

	if err := Chown(YbactlLogFile(), user, user, false); err != nil {
		return fmt.Errorf("could not set ownership of %s: %v", YbactlLogFile(), err)
	}

	if err := createSoftwareInstallDirs(); err != nil {
		return err
	}

	if err := copyBits(version); err != nil {
		return err
	}
	if err := extractPlatformSupportPackageAndYugabundle(version); err != nil {
		return err
	}
	if err := renameThirdPartyDependencies(); err != nil {
		return err
	}

	if err := setupJDK(); err != nil {
		return err
	}
	if err := setJDKEnvironmentVariable(); err != nil {
		return err
	}
	if !HasSudoAccess() {
		log.Info("setup systemd --user for long running services")
		if err := systemd.LingerEnable(); err != nil {
			return err
		}
		// Create a link to network online target for user services, which otherwise cannot depend on it
		if err := systemd.Link("/usr/lib/systemd/system/network-online.target"); err != nil {
			return err
		}
		fp := fmt.Sprintf("/home/%s/.bashrc", viper.GetString("service_username"))
		bashrc, err := os.OpenFile(fp, os.O_APPEND|os.O_WRONLY|os.O_CREATE, 0644)
		if err != nil {
			return fmt.Errorf("could not open /home/%s/bashrc: %w",
				viper.GetString("service_username"), err)
		}
		defer bashrc.Close()
		if _, err := bashrc.WriteString("export XDG_RUNTIME_DIR=/run/user/$(id -u)\n"); err != nil {
			return fmt.Errorf("could not write XDG_RUNTIME_DIR to bashrc: %w", err)
		}
	}
	return nil
}

// Initialize creates does setup of the common data directories.
func Initialize() error {
	if err := createDataInstallDirs(); err != nil {
		return err
	}

	// Validate or create the data version file.
	if err := CheckOrCreateDataVersionFile(); err != nil {
		return err
	}

	// Generate certs if required.
	var serverCertPath, serverKeyPath string
	if len(viper.GetString("server_cert_path")) == 0 {
		log.Info("Generating self-signed server certificates")
		serverCertPath, serverKeyPath = GenerateSelfSignedCerts()
		if err := SetYamlValue(InputFile(), "server_cert_path", serverCertPath); err != nil {
			return err
		}
		if err := SetYamlValue(InputFile(), "server_key_path", serverKeyPath); err != nil {
			return err
		}
		InitViper()
	}
	return nil
}

func createSoftwareInstallDirs() error {
	dirs := []string{
		GetSoftwareRoot(),
		dm.WorkingDirectory(),
		GetInstallerSoftwareDir(),
		GetBaseInstall(),
	}
	if err := CreateDirs(dirs); err != nil {
		return err
	}
	// Remove the symlink if one exists
	SetActiveInstallSymlink()
	return nil
}

func createDataInstallDirs() error {
	dirs := []string{
		filepath.Join(GetBaseInstall(), "data"),
		filepath.Join(GetBaseInstall(), "data/logs"),
	}
	return CreateDirs(dirs)
}

func createUpgradeDirs() error {
	dirs := []string{
		GetInstallerSoftwareDir(),
	}
	return CreateDirs(dirs)
}

func CreateDirs(createDirs []string) error {
	for _, dir := range createDirs {
		_, err := os.Stat(dir)
		if os.IsNotExist(err) {
			if err := MkdirAll(dir, DirMode); err != nil {
				return fmt.Errorf(fmt.Sprintf("failed creating directory %s: %s", dir, err.Error()))
			}
		}
		// Only change ownership for root installs.
		if HasSudoAccess() {
			serviceuser := viper.GetString("service_username")
			err := Chown(dir, serviceuser, serviceuser, false)
			if err != nil {
				return fmt.Errorf("failed to change ownership of " + dir + " to " +
					serviceuser + ": " + err.Error())
			}
		}
	}
	return nil
}

// CheckOrCreateDataVersionFile checks if the YBA Version is older than the data version file. This is not
// supported as an older YBA cannot run with newer data due to PG migrations. If the file does not
// exist, it will instead create it with the current version
func CheckOrCreateDataVersionFile() error {
	_, err := os.Stat(DataVersionFile())
	if os.IsNotExist(err) {
		return os.WriteFile(DataVersionFile(), []byte(Version), 0644)
	}
	return CheckDataVersionFile()
}

// CheckDataVersionFile checks if the YBA Version is older than the data version file. This is not
// supported as an older YBA cannot run with newer data due to PG migrations
func CheckDataVersionFile() error {
	buf, err := os.ReadFile(DataVersionFile())
	if !os.IsNotExist(err) {
		if err != nil {
			return fmt.Errorf("could not open data version file: %w", err)
		}
		dataVersion := strings.TrimSpace(string(buf))
		if LessVersions(Version, dataVersion) {
			return fmt.Errorf("YBA version %s is older than data version %s", Version, dataVersion)
		}
	}
	// Update to the latest version.
	return os.WriteFile(DataVersionFile(), []byte(Version), 0644)
}

// Copies over necessary files for all services from yba_installer_full to the GetSoftwareRoot()
func copyBits(vers string) error {
	yugabundleBinary := "yugabundle-" + vers + "-centos-x86_64.tar.gz"
	neededFiles := []string{GoBinaryName, VersionMetadataJSON, yugabundleBinary,
		GetJavaPackagePath(), GetPostgresPackagePath()}

	for _, file := range neededFiles {
		fp := AbsoluteBundlePath(file)
		if err := Copy(fp, GetInstallerSoftwareDir(), false, true); err != nil {
			return fmt.Errorf("failed to copy " + fp + ": " + err.Error())
		}
	}

	configDest := path.Join(GetInstallerSoftwareDir(), ConfigDir)
	if _, err := os.Stat(configDest); errors.Is(err, os.ErrNotExist) {
		if err := Copy(GetTemplatesDir(), configDest, true, false); err != nil {
			return fmt.Errorf("failed to copy config files: " + err.Error())
		}
	} else {
		log.Debug("skipping template file copy, already exists")
	}

	return nil
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
func Upgrade(version string) error {

	// Change into the dir we are in so that we can specify paths relative to ourselves
	// TODO(minor): probably not a good idea in the long run
	if err := os.Chdir(GetBinaryDir()); err != nil {
		return err
	}
	// Change ownership as part of upgrade to allow non-root commands
	user := viper.GetString("service_username")
	if err := Chown(InputFile(), user, user, false); err != nil {
		return fmt.Errorf("could not set ownership of %s: %v", InputFile(), err)
	}
	if err := Chown(YbactlLogFile(), user, user, false); err != nil {
		return fmt.Errorf("could not set ownership of %s: %v", YbactlLogFile(), err)
	}
	if err := createUpgradeDirs(); err != nil {
		return err
	}
	if err := copyBits(version); err != nil {
		return err
	}
	if err := extractPlatformSupportPackageAndYugabundle(version); err != nil {
		return err
	}
	if err := renameThirdPartyDependencies(); err != nil {
		return err
	}
	if err := setupJDK(); err != nil {
		return err
	}
	if err := setJDKEnvironmentVariable(); err != nil {
		return err
	}

	return nil

}

// PostUpgrade will update the active install symlink and remove old versions if needed
func PostUpgrade() {
	if err := SetActiveInstallSymlink(); err != nil {
		log.Warn("Error setting active symlink. " +
			"Upgrade has completed but backups with version metadata may be affected.")
	}
	PrunePastInstalls()
}

// SetActiveInstallSymlink will create <installRoot>/active symlink
func SetActiveInstallSymlink() error {
	// Remove the symlink if one exists
	if _, err := os.Stat(GetActiveSymlink()); err == nil {
		if err := os.Remove(GetActiveSymlink()); err != nil {
			return fmt.Errorf("failed removing previous symlink: %s", err.Error())
		}
	}
	if err := os.Symlink(GetSoftwareRoot(), GetActiveSymlink()); err != nil {
		return fmt.Errorf("could not create active symlink: %s", err.Error())
	}
	return nil
}

func setupJDK() error {
	dirName, err := javaDirectoryName()
	if err != nil {
		return fmt.Errorf("failed to untar jdk: " + err.Error())
	}
	_, err = os.Stat(filepath.Join(GetInstallerSoftwareDir(), dirName))
	if errors.Is(err, os.ErrNotExist) {
		out := shell.Run("tar", "-zxf", GetJavaPackagePath(), "-C", GetInstallerSoftwareDir())
		if !out.SucceededOrLog() {
			return fmt.Errorf("failed to setup JDK: " + out.Error.Error())
		}
	} else {
		log.Debug("jdk already extracted")
	}
	return nil
}

// When we start Yugaware, we point to the location of our bundled
// JDK via the JAVA_HOME environment variable, so that it knows to
// use the version we provide. Needed for non-root installs as we cannot
// set the environment variables in the systemd service file any longer.
func setJDKEnvironmentVariable() error {
	tarArgs := []string{
		"-tf", GetJavaPackagePath(),
		"|",
		"head", "-n", "1",
	}
	out := shell.RunShell("tar", tarArgs...)
	out.LogDebug()
	if !out.SucceededOrLog() {
		return fmt.Errorf("failed to setup JDK environment: " + out.Error.Error())
	}

	javaExtractedFolderName, err := javaDirectoryName()
	if err != nil {
		return fmt.Errorf("failed to setup JDK Environment: " + err.Error())
	}
	javaHome := GetInstallerSoftwareDir() + javaExtractedFolderName
	if err := os.Setenv("JAVA_HOME", javaHome); err != nil {
		return fmt.Errorf("failed setting JAVA_HOME environment variable: %s", err.Error())
	}
	return nil
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

func createYugabyteUser() error {
	userName := viper.GetString("service_username")
	if userName != DefaultServiceUser {
		log.Debug("skipping creation of non-yugabyte user " + userName)
		return nil
	}

	// Check if the user exists. id command will have non 0 exit status if the user does not exist.
	if out := shell.Run("id", "-u", userName); out.Succeeded() {
		log.Debug("User " + userName + " already exists, skipping user creation.")
		return nil
	}

	if HasSudoAccess() {
		if out := shell.Run("useradd", "-m", userName, "-U"); !out.Succeeded() {
			return fmt.Errorf("failed to create user " + userName + ": " + out.Error.Error())
		}
	} else {
		return fmt.Errorf("need sudo access to create yugabyte user")
	}
	return nil
}

func extractPlatformSupportPackageAndYugabundle(vers string) error {

	log.Info("Extracting yugabundle package.")

	if err := RemoveAll(filepath.Join(GetInstallerSoftwareDir(), "packages")); err != nil {
		return fmt.Errorf("failed to remove old packages: " + err.Error())
	}

	yugabundleBinary := GetInstallerSoftwareDir() + "/yugabundle-" + vers + "-centos-x86_64.tar.gz"

	rExtract1, errExtract1 := os.Open(yugabundleBinary)
	if errExtract1 != nil {
		return fmt.Errorf("Error in starting the File Extraction process. " + errExtract1.Error())
	}
	defer rExtract1.Close()

	log.Debug(fmt.Sprintf("Extracting %s", yugabundleBinary))
	err := tar.Untar(rExtract1, GetInstallerSoftwareDir(), tar.WithMaxUntarSize(-1))
	if err != nil {
		return fmt.Errorf(fmt.Sprintf("failed to extract file %s, error: %s", yugabundleBinary, err.Error()))
	}
	log.Debug(fmt.Sprintf("Completed extracting %s", yugabundleBinary))

	path1 := GetInstallerSoftwareDir() + "/yugabyte-" + vers +
		"/yugabundle_support-" + vers + "-centos-x86_64.tar.gz"

	rExtract2, errExtract2 := os.Open(path1)
	if errExtract2 != nil {
		fmt.Println(errExtract2.Error())
		return fmt.Errorf("error in starting the file extraction process: " + errExtract2.Error())
	}
	defer rExtract2.Close()

	log.Debug(fmt.Sprintf("Extracting %s", path1))
	err = tar.Untar(rExtract2, GetInstallerSoftwareDir(), tar.WithMaxUntarSize(-1))
	if err != nil {
		return fmt.Errorf(fmt.Sprintf("failed to extract file %s, error: %s", path1, err.Error()))
	}
	log.Debug(fmt.Sprintf("Completed extracting %s", path1))

	if err := Rename(GetInstallerSoftwareDir()+"/yugabyte-"+vers,
		GetInstallerSoftwareDir()+"/packages/yugabyte-"+vers); err != nil {
		return err
	}

	if HasSudoAccess() {
		userName := viper.GetString("service_username")
		if err := Chown(GetSoftwareRoot(), userName, userName, true); err != nil {
			return err
		}
	}
	return nil
}

func renameThirdPartyDependencies() error {

	log.Info("Extracting third-party dependencies.")

	//Remove any thirdparty directories if they already exist, so
	//that the install action is idempotent.
	if err := RemoveAll(GetInstallerSoftwareDir() + "/thirdparty"); err != nil {
		return fmt.Errorf("failed to clean thirdparty directory: " + err.Error())
	}
	if err := RemoveAll(GetInstallerSoftwareDir() + "/third-party"); err != nil {
		return fmt.Errorf("failed to clean thirdparty directory: " + err.Error())
	}

	path := GetInstallerSoftwareDir() + "/packages/thirdparty-deps.tar.gz"
	rExtract, _ := os.Open(path)
	log.Debug("Extracting archive " + path)
	if err := tar.Untar(rExtract, GetInstallerSoftwareDir(), tar.WithMaxUntarSize(-1)); err != nil {
		return fmt.Errorf(fmt.Sprintf("failed to extract file %s, error: %s",
			path, err.Error()))
	}

	log.Debug(fmt.Sprintf("Completed extracting archive at %s to %s", path, GetInstallerSoftwareDir()))
	if err := Rename(GetInstallerSoftwareDir()+"/thirdparty",
		GetInstallerSoftwareDir()+"/third-party"); err != nil {
		return err
	}
	return nil
}

// FixConfigValues sets any mandatory config defaults not set by user (generally passwords)
func FixConfigValues() error {

	if len(viper.GetString("service_username")) == 0 {
		log.Info(fmt.Sprintf("Systemd services will be run as user %s", DefaultServiceUser))
		if err := SetYamlValue(InputFile(), "service_username", DefaultServiceUser); err != nil {
			return err
		}
		InitViper()
	}

	if len(viper.GetString("platform.appSecret")) == 0 {
		log.Debug("Generating default app secret for platform")
		if err := SetYamlValue(InputFile(), "platform.appSecret",
			GenerateRandomStringURLSafe(64)); err != nil {
			return err
		}
		InitViper()
	}

	if len(viper.GetString("platform.keyStorePassword")) == 0 {
		log.Debug("Generating default app secret for platform")
		if err := SetYamlValue(InputFile(), "platform.keyStorePassword",
			GenerateRandomStringURLSafe(32)); err != nil {
			return err
		}
		InitViper()
	}

	if len(viper.GetString("host")) == 0 {
		host := GuessPrimaryIP()
		log.Info("Guessing primary IP of host to be " + host)
		if err := SetYamlValue(InputFile(), "host", host); err != nil {
			return err
		}
		InitViper()
	}

	if viper.GetBool("postgres.install.enabled") &&
		len(viper.GetString("postgres.install.password")) == 0 {
		log.Info("Generating default password for postgres")
		if err := SetYamlValue(InputFile(), "postgres.install.password",
			GenerateRandomStringURLSafe(32)); err != nil {
			return err
		}
		InitViper()
	}

	if viper.GetBool("prometheus.enableAuth") &&
		len(viper.GetString("prometheus.authPassword")) == 0 {
		log.Info("Generating default password for prometheus")
		if err := SetYamlValue(InputFile(), "prometheus.authPassword",
			GenerateRandomStringURLSafe(32)); err != nil {
			return err
		}
		InitViper()
	}

	if viper.GetBool("prometheus.enableHttps") {
		// Default to YBA certs
		if err := SetYamlValue(InputFile(), "prometheus.httpsCertPath",
			viper.GetString("server_cert_path")); err != nil {
			return err
		}
		if err := SetYamlValue(InputFile(), "prometheus.httpsKeyPath",
			viper.GetString("server_key_path")); err != nil {
			return err
		}
		InitViper()
	}
	return nil
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
	username := viper.GetString("service_username")
	if err := Chown(certsDir, username, username, true); err != nil {
		log.Fatal(fmt.Sprintf("Unable to chown dir %s", certsDir))
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
func WaitForYBAReady(version string) error {
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

		waitSecs := viper.GetInt("wait_for_yba_ready_secs")
		endTime := time.Now().Add(time.Duration(waitSecs) * time.Second)
		success := false
		for time.Now().Before(endTime) {
			resp, err = http.Get(url)
			if err != nil {
				log.Info(fmt.Sprintf("YBA at %s not ready. Checking again in 10 seconds.", url))
				time.Sleep(10 * time.Second)
			} else {
				success = true
				break
			}
		}
		if !success {
			return fmt.Errorf("YBA at %s not ready after %d minutes", url, waitSecs/60)
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
				return fmt.Errorf("error waiting for YBA ready: %s", err.Error())
			}
		}

	}
	return nil
}
