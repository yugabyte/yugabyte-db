/*
 * Copyright (c) YugabyteDB, Inc.
 */

package cmd

import (
	"errors"
	"fmt"
	"io/fs"
	"os"
	"path/filepath"
	"strings"

	"github.com/fluxcd/pkg/tar"
	"github.com/spf13/viper"

	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/byocproxy"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/common"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/common/shell"
	log "github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/logging"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/systemd"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/template"
)

type byocApiProxyDirectories struct {
	SystemdFileLocation string
	templateFileName    string
	// BaseDir holds one directory per installed version plus the download cache and
	// the 'active' symlink. It lives outside the per-YBA-version software root
	// because the byoc-api-proxy release lifecycle is independent of YBA.
	BaseDir       string
	DownloadDir   string
	ActiveDir     string
	JarPath       string
	ConfDir       string
	AppConfigFile string
	EnvFile       string
	LogDir        string
}

func newByocApiProxyDirectories() byocApiProxyDirectories {
	baseDir := filepath.Join(common.GetBaseInstall(), "byoc-api-proxy")
	activeDir := filepath.Join(baseDir, "active")
	confDir := filepath.Join(common.GetBaseInstall(), "data", "byoc-api-proxy")
	return byocApiProxyDirectories{
		SystemdFileLocation: common.SystemdDir + "/byoc-api-proxy.service",
		templateFileName:    "yba-installer-byoc-api-proxy.yml",
		BaseDir:             baseDir,
		DownloadDir:         filepath.Join(baseDir, "downloads"),
		ActiveDir:           activeDir,
		JarPath:             filepath.Join(activeDir, "bin", "byoc-api-proxy.jar"),
		ConfDir:             confDir,
		AppConfigFile:       filepath.Join(confDir, "application.yaml"),
		EnvFile:             filepath.Join(confDir, "byoc-api-proxy.env"),
		LogDir:              common.GetBaseInstall() + "/data/logs",
	}
}

// ByocApiProxy is the byoc-api-proxy service. Its packages are not part of the
// yba_installer_full bundle: they are downloaded from byocApiProxy.downloadBaseUrl,
// either pinned to a specific version or tracking the latest published release.
//
// The service manages itself best effort so it never fails a yba-ctl workflow:
// expected conditions (configuration not provided yet, configuration rejected by
// the package's validate mode, download site unreachable) are logged and leave
// the service uninstalled or on its previous version, returning nil. Only
// genuine errors (filesystem failures and the like) are returned. Configuration
// requirements live in the release package itself (systemd/install.sh
// --validate), so new required fields in future proxy releases do not need
// yba-ctl changes.
type ByocApiProxy struct {
	name string
	byocApiProxyDirectories
}

// NewByocApiProxy creates the byoc-api-proxy service. Unlike other services the
// version is not fixed at build time; it is resolved during
// install/upgrade/reconfigure (see reconcileVersion).
func NewByocApiProxy() ByocApiProxy {
	return ByocApiProxy{
		name:                    "byoc-api-proxy",
		byocApiProxyDirectories: newByocApiProxyDirectories(),
	}
}

func (ByocApiProxy) IsReplicated() bool { return false }

func (b ByocApiProxy) SystemdFile() string {
	return b.SystemdFileLocation
}

func (b ByocApiProxy) TemplateFile() string {
	return b.templateFileName
}

func (b ByocApiProxy) Name() string {
	return b.name
}

// Version returns the installed byoc-api-proxy version, or "" when none: the
// 'active' symlink points at BaseDir/<version>.
func (b ByocApiProxy) Version() string {
	target, err := filepath.EvalSymlinks(b.ActiveDir)
	if err != nil {
		return ""
	}
	version := filepath.Base(target)
	if !byocproxy.IsValidVersion(version) {
		return ""
	}
	return version
}

func (b ByocApiProxy) downloadBaseURL() string {
	return viper.GetString("byocApiProxy.downloadBaseUrl")
}

func (b ByocApiProxy) targetVersion() (string, error) {
	return byocproxy.ResolveVersion(b.downloadBaseURL(), viper.GetString("byocApiProxy.version"))
}

func (b ByocApiProxy) Install() error {
	log.Info("Starting byoc-api-proxy install")
	if err := b.reconcileAndFinish(); err != nil {
		return err
	}
	log.Info("Finishing byoc-api-proxy install")
	return nil
}

func (b ByocApiProxy) Initialize() error {
	log.Info("Starting byoc-api-proxy initialize")
	if err := b.Start(); err != nil {
		return err
	}
	log.Info("Finishing byoc-api-proxy initialize")
	return nil
}

// Upgrade reconciles byoc-api-proxy to the correct state during a YBA upgrade. A
// version change (a new release on the "latest" train, or an edited pin in
// yba-ctl.yml) is applied here, and a service enabled but never installed (e.g.
// configuration was missing at install time) is installed from scratch.
func (b ByocApiProxy) Upgrade() error {
	log.Info("Starting byoc-api-proxy upgrade")
	b.byocApiProxyDirectories = newByocApiProxyDirectories()
	if err := b.reconcileAndFinish(); err != nil {
		return err
	}
	if err := b.Start(); err != nil {
		return err
	}
	log.Info("Finished byoc-api-proxy upgrade")
	return nil
}

// Reconfigure reconciles byoc-api-proxy to the correct state, applying
// byocApiProxy.version and byocApiProxy.appConfig changes made in yba-ctl.yml.
// The service is started by the reconfigure flow afterwards.
func (b ByocApiProxy) Reconfigure() error {
	log.Info("Reconfiguring byoc-api-proxy")
	if err := b.reconcileAndFinish(); err != nil {
		return err
	}
	if err := systemd.DaemonReload(); err != nil {
		return fmt.Errorf("failed to reload systemd daemon: %w", err)
	}
	log.Info("byoc-api-proxy reconfigured")
	return nil
}

// reconcileAndFinish drives byoc-api-proxy to the state yba-ctl.yml asks for. A
// new version is validated before activation so a bad release keeps the current
// one running. Expected conditions (not configured yet, invalid configuration,
// unreachable download site) log a warning and leave the service uninstalled or
// unchanged; only genuine errors are returned.
func (b ByocApiProxy) reconcileAndFinish() error {
	if b.Version() == "" && !b.isConfigured() {
		log.Warn("Skipping byoc-api-proxy: not configured yet. Set byocApiProxy.appConfig in " +
			common.InputFile() + " and run 'yba-ctl reconfigure'.")
		return nil
	}
	if err := b.prepareConfigs(); err != nil {
		return err
	}
	validated := b.reconcileVersion()
	if b.Version() == "" {
		// Nothing was installed and no version could be installed; reconcileVersion
		// already logged why.
		return nil
	}
	b.pruneOldReleases()
	if !validated {
		if err := b.validateConfig(b.ActiveDir); err != nil {
			log.Warn("Deactivating byoc-api-proxy until its configuration is fixed: " + err.Error())
			return b.deactivate()
		}
	}
	return b.finishInstall()
}

// deactivate stops the service and removes its systemd unit (keeping the
// downloaded packages and config files), returning it to the not-installed
// state until a reconfigure with valid configuration brings it back.
func (b ByocApiProxy) deactivate() error {
	if err := b.Stop(); err != nil {
		log.Warn("failed to stop byoc-api-proxy: " + err.Error())
	}
	if err := os.Remove(b.SystemdFileLocation); err != nil && !errors.Is(err, fs.ErrNotExist) {
		return fmt.Errorf("failed to remove %s: %w", b.SystemdFileLocation, err)
	}
	return systemd.DaemonReload()
}

func (b ByocApiProxy) PreUpgrade() error { return nil }

func (b ByocApiProxy) Start() error {
	if !common.Exists(b.SystemdFileLocation) {
		log.Debug("byoc-api-proxy is not installed, skipping start")
		return nil
	}
	serviceName := filepath.Base(b.SystemdFileLocation)
	if err := systemd.DaemonReload(); err != nil {
		return fmt.Errorf("failed to start byoc-api-proxy: %w", err)
	}
	if err := systemd.Enable(false, serviceName); err != nil {
		return fmt.Errorf("failed to start byoc-api-proxy: %w", err)
	}
	if err := systemd.Start(serviceName); err != nil {
		return fmt.Errorf("failed to start byoc-api-proxy: %w", err)
	}
	log.Debug("started byoc-api-proxy")
	return nil
}

func (b ByocApiProxy) Stop() error {
	serviceName := filepath.Base(b.SystemdFileLocation)
	status, err := b.Status()
	if err != nil {
		return err
	}
	if status.Status != common.StatusRunning {
		log.Debug(b.name + " is already stopped")
		return nil
	}
	if err := systemd.Stop(serviceName); err != nil {
		return fmt.Errorf("failed to stop byoc-api-proxy: %w", err)
	}
	log.Info("stopped byoc-api-proxy")
	return nil
}

func (b ByocApiProxy) Restart() error {
	if !common.Exists(b.SystemdFileLocation) {
		log.Debug("byoc-api-proxy is not installed, skipping restart")
		return nil
	}
	log.Info("Restarting byoc-api-proxy..")
	serviceName := filepath.Base(b.SystemdFileLocation)
	if err := systemd.DaemonReload(); err != nil {
		return fmt.Errorf("failed to restart byoc-api-proxy: %w", err)
	}
	if err := systemd.Restart(serviceName); err != nil {
		return fmt.Errorf("failed to restart byoc-api-proxy: %w", err)
	}
	return nil
}

func (b ByocApiProxy) Uninstall(removeData bool) error {
	log.Info("Uninstalling byoc-api-proxy")
	if err := b.Stop(); err != nil {
		log.Warn("failed to stop byoc-api-proxy, continuing with uninstall: " + err.Error())
	}
	if err := os.Remove(b.SystemdFileLocation); err != nil && !errors.Is(err, fs.ErrNotExist) {
		log.Info(fmt.Sprintf("Error %s removing systemd service %s.",
			err.Error(), b.SystemdFileLocation))
		return err
	}
	if err := systemd.DaemonReload(); err != nil {
		return fmt.Errorf("failed to uninstall byoc-api-proxy: %w", err)
	}
	// The installed packages are re-downloadable, remove them unconditionally.
	if err := common.RemoveAll(b.BaseDir); err != nil {
		log.Info(fmt.Sprintf("Error %s removing byoc-api-proxy software dir %s.",
			err.Error(), b.BaseDir))
	}
	if removeData {
		if err := common.RemoveAll(b.ConfDir); err != nil {
			log.Info(fmt.Sprintf("Error %s removing byoc-api-proxy config dir %s.",
				err.Error(), b.ConfDir))
		}
	}
	return nil
}

func (b ByocApiProxy) Status() (common.Status, error) {
	status := common.Status{
		Service:    b.Name(),
		Version:    b.Version(),
		ConfigLoc:  b.AppConfigFile,
		LogFileLoc: b.LogDir + "/byoc-api-proxy.log",
		BinaryLoc:  b.JarPath,
	}
	status.ServiceFileLoc = b.SystemdFileLocation

	props, err := systemd.Show(filepath.Base(b.SystemdFileLocation), "LoadState", "SubState",
		"ActiveState", "ActiveEnterTimestamp", "ActiveExitTimestamp")
	if err != nil {
		log.Error("Failed to get byoc-api-proxy status: " + err.Error())
		return status, err
	}
	if props["LoadState"] == "not-found" {
		status.Status = common.StatusNotInstalled
	} else if props["SubState"] == "running" {
		status.Status = common.StatusRunning
		status.Since = common.StatusSince(props["ActiveEnterTimestamp"])
	} else if props["ActiveState"] == "inactive" {
		status.Status = common.StatusStopped
		status.Since = common.StatusSince(props["ActiveExitTimestamp"])
	} else {
		status.Status = common.StatusErrored
		status.Since = common.StatusSince(props["ActiveExitTimestamp"])
	}
	return status, nil
}

// isConfigured reports whether byocApiProxy.appConfig has been provided. Knows
// nothing about specific fields: requirements belong to the release package's
// validate mode.
func (b ByocApiProxy) isConfigured() bool {
	return len(viper.GetStringMap("byocApiProxy.appConfig")) > 0
}

// validateConfig dry-runs the given package version against the current
// configuration using the package's install script in validate mode (which
// runs the app's own --validate-config), so the package itself - not yba-ctl -
// defines what configuration is required.
func (b ByocApiProxy) validateConfig(versionDir string) error {
	script := filepath.Join(versionDir, "systemd", "install.sh")
	args := []string{
		"--validate",
		"--app-config", b.AppConfigFile,
		"--env-file", b.EnvFile,
	}
	if javaHome := installedJavaHome(); javaHome != "" {
		args = append(args, "--java-home", javaHome)
	}
	var out *shell.Output
	if common.HasSudoAccess() {
		out = shell.RunAsUser(viper.GetString("service_username"), script, args...)
	} else {
		out = shell.Run(script, args...)
	}
	if out.ExitCode == 0 {
		log.Debug("byoc-api-proxy configuration validated against " + versionDir)
		return nil
	}
	return fmt.Errorf("configuration rejected by %s (exit %d): %s",
		script, out.ExitCode, lastOutputLines(out, 10))
}

// installedJavaHome returns the extracted JDK under the software dir - the same
// JDK the systemd unit template points at. The common JDK helpers
// (javaDirectoryName) inspect the installer bundle tarball, which does not exist
// during reconfigure, so glob the extracted dir instead. "" falls back to PATH.
func installedJavaHome() string {
	matches, _ := filepath.Glob(filepath.Join(common.GetInstallerSoftwareDir(), "jdk*"))
	if len(matches) == 0 {
		return ""
	}
	return matches[0]
}

func lastOutputLines(out *shell.Output, n int) string {
	combined := strings.TrimSpace(out.StdoutString() + "\n" + out.StderrString())
	var lines []string
	for _, line := range strings.Split(combined, "\n") {
		if strings.TrimSpace(line) != "" {
			lines = append(lines, strings.TrimSpace(line))
		}
	}
	if len(lines) > n {
		lines = lines[len(lines)-n:]
	}
	return strings.Join(lines, "\n    ")
}

// reconcileVersion moves the install to the version yba-ctl.yml asks for,
// validating the new version against the current configuration before
// activation. Any failure only logs a warning and keeps the installed version
// (or leaves the service uninstalled). Returns true when a new version was
// validated and activated.
func (b ByocApiProxy) reconcileVersion() bool {
	installed := b.Version()
	target, err := b.targetVersion()
	if err != nil {
		if installed == "" {
			log.Warn("Skipping byoc-api-proxy: failed to resolve version: " + err.Error())
		} else {
			log.Warn(fmt.Sprintf("Could not resolve byoc-api-proxy version, keeping installed "+
				"version %s: %s", installed, err.Error()))
		}
		return false
	}
	if target == installed {
		log.Debug("byoc-api-proxy already at version " + target)
		return false
	}
	log.Info(fmt.Sprintf("Moving byoc-api-proxy from version '%s' to version '%s'", installed,
		target))
	versionDir, err := b.stageVersion(target)
	if err == nil {
		err = b.validateConfig(versionDir)
	}
	if err == nil {
		err = b.activateVersion(versionDir)
	}
	if err != nil {
		if installed == "" {
			log.Warn(fmt.Sprintf("Skipping byoc-api-proxy: could not install version %s: %s",
				target, err.Error()))
		} else {
			log.Warn(fmt.Sprintf("Could not move byoc-api-proxy to version %s, keeping installed "+
				"version %s: %s", target, installed, err.Error()))
		}
		return false
	}
	return true
}

// stageVersion makes BaseDir/<version> available (downloading and extracting the
// release package as needed) without activating it. A release tarball already
// present in the download cache is used without re-downloading, which also
// allows manually dropping packages on airgapped hosts.
func (b ByocApiProxy) stageVersion(version string) (string, error) {
	if err := common.CreateDirs([]string{b.BaseDir, b.DownloadDir}); err != nil {
		return "", err
	}

	versionDir := filepath.Join(b.BaseDir, version)
	jarPath := filepath.Join(versionDir, "bin", "byoc-api-proxy.jar")
	if common.Exists(jarPath) {
		log.Debug("byoc-api-proxy version " + version + " already extracted at " + versionDir)
		return versionDir, nil
	}

	pkgPath := filepath.Join(b.DownloadDir, byocproxy.PackageName(version))
	if !common.Exists(pkgPath) {
		log.Info("Downloading " + byocproxy.PackageURL(b.downloadBaseURL(), version))
		var err error
		pkgPath, err = byocproxy.DownloadPackage(b.downloadBaseURL(), version, b.DownloadDir)
		if err != nil {
			return "", err
		}
	}
	// The release tarball has a single top level <version>/ directory.
	rExtract, err := os.Open(pkgPath)
	if err != nil {
		return "", fmt.Errorf("failed to open %s: %w", pkgPath, err)
	}
	defer rExtract.Close()
	if err := tar.Untar(rExtract, b.BaseDir, tar.WithMaxUntarSize(-1)); err != nil {
		// Drop the bad package so the next run re-downloads instead of re-failing.
		os.Remove(pkgPath)
		return "", fmt.Errorf("failed to extract %s: %w", pkgPath, err)
	}
	if !common.Exists(jarPath) {
		os.Remove(pkgPath)
		return "", fmt.Errorf("%s not found after extracting %s", jarPath, pkgPath)
	}
	if common.HasSudoAccess() {
		userName := viper.GetString("service_username")
		if err := common.Chown(b.BaseDir, userName, userName, true); err != nil {
			return "", fmt.Errorf("failed to change ownership of %s: %w", b.BaseDir, err)
		}
	}
	return versionDir, nil
}

func (b ByocApiProxy) pruneOldReleases() {
	removed, err := byocproxy.PruneOldReleases(b.BaseDir, b.DownloadDir, b.Version())
	for _, path := range removed {
		log.Info("Removed old byoc-api-proxy release " + path)
	}
	if err != nil {
		log.Warn("failed to prune old byoc-api-proxy releases: " + err.Error())
	}
}

func (b ByocApiProxy) activateVersion(versionDir string) error {
	if err := common.Symlink(versionDir, b.ActiveDir); err != nil {
		return fmt.Errorf("failed to point %s at %s: %w", b.ActiveDir, versionDir, err)
	}
	return nil
}

// prepareConfigs generates application.yaml from byocApiProxy.appConfig. This
// runs before validation so the dry-run sees exactly what the service would.
func (b ByocApiProxy) prepareConfigs() error {
	if err := common.CreateDirs([]string{b.ConfDir}); err != nil {
		return err
	}
	if err := b.writeAppConfig(); err != nil {
		return err
	}
	if common.HasSudoAccess() {
		userName := viper.GetString("service_username")
		if err := common.Chown(b.ConfDir, userName, userName, true); err != nil {
			return fmt.Errorf("failed to change ownership of %s: %w", b.ConfDir, err)
		}
	}
	return nil
}

// finishInstall seeds the operator-owned env file and regenerates templated
// files (systemd unit, logrotate config). Called once the desired version is
// active.
func (b ByocApiProxy) finishInstall() error {
	if err := b.seedConfigFiles(); err != nil {
		return err
	}
	if err := template.GenerateTemplate(b); err != nil {
		return fmt.Errorf("failed to generate byoc-api-proxy template: %w", err)
	}
	if common.HasSudoAccess() {
		userName := viper.GetString("service_username")
		for _, dir := range []string{b.BaseDir, b.ConfDir} {
			if err := common.Chown(dir, userName, userName, true); err != nil {
				return fmt.Errorf("failed to change ownership of %s: %w", dir, err)
			}
		}
	}
	return nil
}

// appConfigDefaults returns the application.yaml values yba-ctl derives from
// the local YBA install; the proxy's bundled defaults for these are localhost
// dev placeholders that cannot reach a real YBA. Anything set in
// byocApiProxy.appConfig overrides these.
func (b ByocApiProxy) appConfigDefaults() map[string]interface{} {
	host := "localhost"
	if hosts := common.SplitInput(viper.GetString("host")); len(hosts) > 0 && hosts[0] != "" {
		host = hosts[0]
	}
	// Trust the cert YBA actually serves: the operator's cert when configured,
	// else the generated self-signed CA. The proxy skips hostname verification
	// on this internal hop, so only chain trust matters, not the cert's SAN.
	certPath := viper.GetString("server_cert_path")
	if certPath == "" {
		certPath = common.GetSelfSignedCACertPath()
	}
	return map[string]interface{}{
		"yba": map[string]interface{}{
			"base_url": fmt.Sprintf("https://%s:%s/api", host, viper.GetString("platform.port")),
		},
		"spring": map[string]interface{}{
			"ssl": map[string]interface{}{
				"bundle": map[string]interface{}{
					"pem": map[string]interface{}{
						"yba": map[string]interface{}{
							"truststore": map[string]interface{}{
								"certificate": "file:" + certPath,
							},
						},
					},
				},
			},
		},
	}
}

// writeAppConfig generates the application.yaml overlay: installer-derived
// defaults with byocApiProxy.appConfig deep-merged on top. The file is owned by
// yba-ctl and regenerated on every install, upgrade and reconfigure.
func (b ByocApiProxy) writeAppConfig() error {
	rendered, err := byocproxy.RenderAppConfig(
		b.appConfigDefaults(), viper.GetStringMap("byocApiProxy.appConfig"))
	if err != nil {
		return err
	}
	if err := os.WriteFile(b.AppConfigFile, rendered, 0600); err != nil {
		return fmt.Errorf("failed to write %s: %w", b.AppConfigFile, err)
	}
	return nil
}

// seedConfigFiles creates the operator-owned env file from the example that
// ships in the release package. An existing file is never overwritten. The env
// file is an escape hatch - the primary configuration channel is
// byocApiProxy.appConfig in yba-ctl.yml.
func (b ByocApiProxy) seedConfigFiles() error {
	examplesDir := filepath.Join(b.ActiveDir, "systemd")
	if !common.Exists(b.EnvFile) {
		if err := b.seedEnvFile(filepath.Join(examplesDir, "byoc-api-proxy.env.example")); err != nil {
			return fmt.Errorf("failed to seed %s: %w", b.EnvFile, err)
		}
	}
	return nil
}

// seedEnvFile copies the packaged env example with every assignment commented
// out: the example's active values (standalone-JDK JAVA_HOME, placeholder URLs)
// would fight the systemd unit's JAVA_HOME and the yba-ctl managed
// application.yaml.
func (b ByocApiProxy) seedEnvFile(examplePath string) error {
	data, err := os.ReadFile(examplePath)
	if err != nil {
		return err
	}
	lines := strings.Split(string(data), "\n")
	for i, line := range lines {
		trimmed := strings.TrimSpace(line)
		if trimmed != "" && !strings.HasPrefix(trimmed, "#") {
			lines[i] = "# " + line
		}
	}
	return os.WriteFile(b.EnvFile, []byte(strings.Join(lines, "\n")), 0600)
}
