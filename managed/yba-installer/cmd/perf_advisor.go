package cmd

import (
	"errors"
	"fmt"
	"io/fs"
	"os"
	"path/filepath"

	"github.com/fluxcd/pkg/tar"
	"github.com/spf13/viper"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/common"
	log "github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/logging"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/systemd"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/template"
)

type perfAdvisorDirectories struct {
	SystemdFileLocation string
	templateFileName    string
	ConfFileLocation    string
	PABin               string
	PALogDir            string
}

type PerfAdvisor struct {
	name    string
	version string
	perfAdvisorDirectories
}

// newPerfAdvisorDirectories initializes and returns the perfAdvisorDirectories struct with default paths.
func newPerfAdvisorDirectories(version string) perfAdvisorDirectories {
	return perfAdvisorDirectories{
		SystemdFileLocation: common.SystemdDir + "/yb-perf-advisor.service",
		ConfFileLocation:    common.GetSoftwareRoot() + "/perf-advisor/conf/overrides.properties",
		templateFileName:    "yb-installer-perf-advisor.yml",
		// GetSoftwareRoot returns /opt/yugabyte/software/
		PABin:    common.GetSoftwareRoot() + "/perf-advisor/backend/bin",
		PALogDir: common.GetBaseInstall() + "/data/logs",
	}
}

// NewPerfAdvisor creates and returns a new PerfAdvisor struct for the given version.
func NewPerfAdvisor(version string) PerfAdvisor {
	return PerfAdvisor{
		name:                   "yb-perf-advisor",
		version:                version,
		perfAdvisorDirectories: newPerfAdvisorDirectories(version),
	}
}

// SystemdFile returns the path to the systemd service file for Perf Advisor.
func (perf PerfAdvisor) SystemdFile() string {
	return perf.SystemdFileLocation
}

// TemplateFile returns the name of the template file for the Perf Advisor service.
func (perf PerfAdvisor) TemplateFile() string {
	return perf.templateFileName
}

// Name returns the name of the Perf Advisor service.
func (perf PerfAdvisor) Name() string {
	return perf.name
}

// Version returns the version of the Perf Advisor service.
func (perf PerfAdvisor) Version() string {
	return perf.version
}

// Initialize starts the Perf Advisor service and logs the initialization process.
func (perf PerfAdvisor) Initialize() error {
	log.Info("Starting Perf Advisor initialize")

	if err := perf.Start(); err != nil {
		return err
	}
	log.Info("Finishing Perf Advisor initialize")
	return nil
}

// Start enables and starts the Perf Advisor systemd service.
func (perf PerfAdvisor) Start() error {
	serviceName := filepath.Base(perf.SystemdFileLocation)
	if err := systemd.DaemonReload(); err != nil {
		return fmt.Errorf("failed to start perf advisor: %w", err)
	}
	if err := systemd.Enable(false, serviceName); err != nil {
		return fmt.Errorf("failed to start perf advisor: %w", err)
	}
	if err := systemd.Start(serviceName); err != nil {
		return fmt.Errorf("failed to start perf advisor: %w", err)
	}
	log.Debug("started Perf Advisor")
	return nil
}

// Stop disables and stops the Perf Advisor systemd service.
func (perf PerfAdvisor) Stop() error {
	serviceName := filepath.Base(perf.SystemdFileLocation)
	status, err := perf.Status()
	if err != nil {
		return err
	}
	if status.Status != common.StatusRunning {
		log.Debug(perf.name + " is already stopped")
		return nil
	}
	if err := systemd.Stop(serviceName); err != nil {
		return fmt.Errorf("failed to stop perf advisor: %w", err)
	}
	log.Info("stopped perf advisor")
	return nil
}

// Status prints the status output specific to Perf Advisor.
func (perf PerfAdvisor) Status() (common.Status, error) {
	// Initialize a Status struct with service name, port, and version.
	status := common.Status{
		Service:    perf.Name(),
		Port:       viper.GetInt("perfAdvisor.port"),
		Version:    perf.Version(),
		LogFileLoc: common.GetBaseInstall() + "/data/logs/perf-advisor.log",
		ConfigLoc:  perf.ConfFileLocation,
		BinaryLoc:  perf.PABin,
	}

	// Set the systemd service file location if one exists
	status.ServiceFileLoc = perf.SystemdFileLocation

	// Query systemd for the service's properties.
	props, err := systemd.Show(filepath.Base(perf.SystemdFileLocation), "LoadState", "SubState",
		"ActiveState", "ActiveEnterTimestamp", "ActiveExitTimestamp")
	if err != nil {
		// Log and return error if unable to get service status.
		log.Error("Failed to get perf advisor status: " + err.Error())
		return status, err
	}
	// If the service is not found, mark as not installed.
	if props["LoadState"] == "not-found" {
		status.Status = common.StatusNotInstalled
		// If the service is running, mark as running and set the start timestamp.
	} else if props["SubState"] == "running" {
		status.Status = common.StatusRunning
		status.Since = common.StatusSince(props["ActiveEnterTimestamp"])
		// If the service is inactive, mark as stopped and set the stop timestamp.
	} else if props["ActiveState"] == "inactive" {
		status.Status = common.StatusStopped
		status.Since = common.StatusSince(props["ActiveExitTimestamp"])
		// For any other state, mark as errored and set the last exit timestamp.
	} else {
		status.Status = common.StatusErrored
		status.Since = common.StatusSince(props["ActiveExitTimestamp"])
	}
	// Return the populated status struct and nil error.
	return status, nil
}

// Uninstall stops, disables, and reloads systemd for the Perf Advisor service.
func (perf PerfAdvisor) Uninstall(removeData bool) error {
	log.Info("Uninstalling perf advisor")
	if err := perf.Stop(); err != nil {
		log.Warn("failed to stop perf advisor, continuing with uninstall: " + err.Error())
	}

	err := os.Remove(perf.SystemdFileLocation)
	if err != nil {
		pe := err.(*fs.PathError)
		if !errors.Is(pe.Err, fs.ErrNotExist) {
			log.Info(fmt.Sprintf("Error %s removing systemd service %s.",
				err.Error(), perf.SystemdFileLocation))
			return err
		}
		// reload systemd daemon
		if err := systemd.DaemonReload(); err != nil {
			return fmt.Errorf("failed to uninstall perf advisor: %w", err)
		}
	}

	if removeData {

	}
	return nil
}

func (perf PerfAdvisor) createSoftwareDirectories() error {
	// Build a list of directories to create (here, just yb-platform under the software root)
	dirs := []string{
		common.GetSoftwareRoot() + "/perf-advisor",
		common.GetSoftwareRoot() + "/perf-advisor/config",
	}
	// Create the directories on disk (if they don't already exist)
	return common.CreateDirs(dirs)
}

func (perf PerfAdvisor) untarAndSetupPerfAdvisorPackages() error {
	// Get the absolute path to the perf_advisor tarball with version in the filename
	paTarball := fmt.Sprintf("perf_advisor-%s.tar.gz", perf.version)
	paPath := common.AbsoluteBundlePath(paTarball)
	targetDir := common.GetSoftwareRoot() + "/perf-advisor"

	// Untar pa.tar.gz into perf-advisor
	rExtract, err := os.Open(paPath)
	if err != nil {
		return fmt.Errorf("failed to open %s: %w", paPath, err)
	}
	defer rExtract.Close()
	if err := tar.Untar(rExtract, targetDir, tar.WithMaxUntarSize(-1)); err != nil {
		return fmt.Errorf("failed to extract %s: %w", paPath, err)
	}

	// Now check that backend and ui/frontend exist
	backendDir := filepath.Join(targetDir, "backend")
	frontendDir := filepath.Join(targetDir, "ui")

	if stat, err := os.Stat(backendDir); err != nil || !stat.IsDir() {
		return fmt.Errorf("backend directory not found in %s after extraction", targetDir)
	}
	if stat, err := os.Stat(frontendDir); err != nil || !stat.IsDir() {
		return fmt.Errorf("ui directory not found in %s after extraction", targetDir)
	}

	if common.HasSudoAccess() {
		userName := viper.GetString("service_username")
		perfAdvisorDir := common.GetSoftwareRoot() + "/perf-advisor"
		// true in this context means recursive, changes permissions of all files and directories inside perf-advisor
		if err := common.Chown(perfAdvisorDir, userName, userName, true); err != nil {
			log.Error(fmt.Sprintf("failed to change ownership of %s to user/group %s: %s", perfAdvisorDir, userName, err.Error()))
			return err
		}
	}
	return nil
}

func (perf PerfAdvisor) Install() error {
	log.Info("Starting Perf Advisor install")
	if err := ensurePerfAdvisorTLSKeystore(); err != nil {
		return fmt.Errorf("ensure Perf Advisor TLS keystore: %w", err)
	}
	template.GenerateTemplate(perf)
	if err := ensurePerfAdvisorConfigOwnership(); err != nil {
		return fmt.Errorf("set perf-advisor config ownership: %w", err)
	}

	if err := perf.createSoftwareDirectories(); err != nil {
		return err
	}

	if err := perf.untarAndSetupPerfAdvisorPackages(); err != nil {
		return err
	}

	// Do root based install or non-root based install
	// When we do root based install, we want file and folders owned by YB user
	if common.HasSudoAccess() {
		// Allow yugabyte user to fully manage this installation (GetBaseInstall() to be safe)
		userName := viper.GetString("service_username")
		if err := changeAllPermissions(userName); err != nil {
			log.Error("Failed to set ownership of " + common.GetBaseInstall() + ": " + err.Error())
			return err
		}
	}

	log.Info("Finishing Perf Advisor install")
	return nil
}

func (perf PerfAdvisor) MigrateFromReplicated() error   { return nil }
func (perf PerfAdvisor) FinishReplicatedMigrate() error { return nil }
func (perf PerfAdvisor) PreUpgrade() error              { return nil }

// Upgrade will upgrade the perf advisor and install it into the alt install directory.
// Upgrade will NOT restart the service, the old version is expected to still be running
func (perf PerfAdvisor) Upgrade() error {
	log.Info("Starting Perf Advisor upgrade")
	perf.perfAdvisorDirectories = newPerfAdvisorDirectories(perf.version)
	if err := template.GenerateTemplate(perf); err != nil {
		return err
	} // systemctl reload is not needed, start handles it for us.
	if err := perf.createSoftwareDirectories(); err != nil {
		return err
	}
	if err := perf.untarAndSetupPerfAdvisorPackages(); err != nil {
		return err
	}

	if common.HasSudoAccess() {
		// Allow yugabyte user to fully manage this installation (GetBaseInstall() to be safe)
		userName := viper.GetString("service_username")
		if err := changeAllPermissions(userName); err != nil {
			log.Error("Failed to set ownership of " + common.GetBaseInstall() + ": " + err.Error())
			return err
		}
	}
	err := perf.Start()
	log.Info("Finished Perf Advisor upgrade")
	return err
}

func (perf PerfAdvisor) Reconfigure() error {
	log.Info("Reconfiguring Perf Advisor")
	if err := ensurePerfAdvisorTLSKeystore(); err != nil {
		return fmt.Errorf("ensure Perf Advisor TLS keystore: %w", err)
	}
	if err := template.GenerateTemplate(perf); err != nil {
		return fmt.Errorf("failed to generate template: %w", err)
	}
	if err := ensurePerfAdvisorConfigOwnership(); err != nil {
		return fmt.Errorf("set perf-advisor config ownership: %w", err)
	}

	// Reload systemd daemon to pick up the regenerated service file
	if err := systemd.DaemonReload(); err != nil {
		return fmt.Errorf("failed to reload systemd daemon: %w", err)
	}
	log.Info("Perf Advisor reconfigured")
	return nil
}

func (PerfAdvisor) IsReplicated() bool { return false }

// ensurePerfAdvisorConfigOwnership chowns baseInstall/perf-advisor (config, certs) to the service user
// so generated files like metrics-export.yml are not root-owned.
func ensurePerfAdvisorConfigOwnership() error {
	if !common.HasSudoAccess() {
		return nil
	}
	dir := common.GetPerfAdvisorBaseDataDir()
	if _, err := os.Stat(dir); err != nil {
		if os.IsNotExist(err) {
			return nil
		}
		return err
	}
	userName := viper.GetString("service_username")
	if err := common.Chown(dir, userName, userName, true); err != nil {
		return err
	}
	return nil
}

// ensurePerfAdvisorTLSKeystore creates the tls.p12 keystore when Perf Advisor TLS is enabled,
// using the platform server cert and key. No-op when perfAdvisor.tls.enabled is false.
func ensurePerfAdvisorTLSKeystore() error {
	if !viper.GetBool("perfAdvisor.tls.enabled") {
		return nil
	}
	certPath, keyPath := common.GetPlatformServerCertPaths()
	if _, err := os.Stat(certPath); err != nil {
		return fmt.Errorf("platform server cert not found at %s: %w", certPath, err)
	}
	if _, err := os.Stat(keyPath); err != nil {
		return fmt.Errorf("platform server key not found at %s: %w", keyPath, err)
	}
	// FixConfigValues() in common.Install() (and reconfigure) generates this password when empty
	// and calls InitViper(), so it is already set by the time we run here.
	password := viper.GetString("perfAdvisor.tls.keystorePassword")
	return common.GeneratePerfAdvisorTLSKeystore(certPath, keyPath, common.GetPerfAdvisorCertsDir(), password)
}

// Restart the perf advisor service.
func (perf PerfAdvisor) Restart() error {
	log.Info("Restarting perf advisor..")
	serviceName := filepath.Base(perf.SystemdFileLocation)
	if err := systemd.DaemonReload(); err != nil {
		return fmt.Errorf("failed to restart perf advisor: %w", err)
	}
	if err := systemd.Restart(serviceName); err != nil {
		return fmt.Errorf("failed to restart perf advisor: %w", err)
	}
	return nil
}
