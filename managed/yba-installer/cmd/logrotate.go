package cmd

import (
	"fmt"
	"os"
	"os/exec"
	"path/filepath"

	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/common"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/template"

	log "github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/logging"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/systemd"
)

type logrotateDirectories struct {
	SystemdFileLocation      string
	SystemdTimerFileLocation string
	ConfFileLocation         string
	StateFileLocation        string
	TemplateFileName         string
	LogrotateScript          string // Path to the logrotate script called by the service
	LogrotateBin             string // Path to the logrotate binary - /usr/sbin/logrotate for example
}

func newLogrotateDirectories() logrotateDirectories {
	softwareRoot := common.GetSoftwareRoot()
	binFile, err := exec.LookPath("logrotate")
	if err != nil {
		binFile = "/usr/sbin/logrotate" // Default path for logrotate binary
		log.Warn("Failed to get executable path: " + err.Error() + ", using default path: " + binFile)
	}
	return logrotateDirectories{
		SystemdFileLocation:      common.SystemdDir + "/yb-logrotate.service",
		SystemdTimerFileLocation: common.SystemdDir + "/yb-logrotate.timer",
		ConfFileLocation:         softwareRoot + "/logrotate/conf/logrotate.conf",
		StateFileLocation:        softwareRoot + "/logrotate/conf/logrotate.status",
		TemplateFileName:         "yba-installer-logrotate.yml",
		LogrotateScript:          filepath.Join(softwareRoot, "logrotate", "bin", "logrotate.sh"),
		LogrotateBin:             binFile,
	}
}

type LogRotate struct {
	logrotateDirectories
}

func NewLogRotate() LogRotate {
	dirs := newLogrotateDirectories()
	return LogRotate{
		logrotateDirectories: dirs,
	}
}

func (l LogRotate) TemplateFile() string { return l.TemplateFileName }
func (l LogRotate) Name() string         { return LogRotateServiceName }
func (l LogRotate) Version() string      { return "" }
func (l LogRotate) SystemdFile() string  { return l.SystemdFileLocation }
func (l LogRotate) IsReplicated() bool   { return false }

func (l LogRotate) Uninstall(cleanData bool) error {
	// Remove all files related to logrotate
	if err := common.RemoveAll(filepath.Join(common.GetSoftwareRoot(), "logrotate")); err != nil {
		return fmt.Errorf("failed to remove logrotate directory: %w", err)
	}
	// Remove systemd service and timer files
	if err := common.RemoveAll(l.SystemdFileLocation); err != nil {
		return fmt.Errorf("failed to remove systemd service file: %w", err)
	}
	if err := common.RemoveAll(l.SystemdTimerFileLocation); err != nil {
		return fmt.Errorf("failed to remove systemd timer file: %w", err)
	}
	return nil
}
func (l LogRotate) Upgrade() error {
	l.logrotateDirectories = newLogrotateDirectories()
	if err := template.GenerateTemplate(l); err != nil {
		return fmt.Errorf("failed to generate logrotate template: %w", err)
	}
	// Make the logrotate.sh script executable
	if err := os.Chmod(l.LogrotateScript, 0755); err != nil {
		return fmt.Errorf("failed to set permissions on logrotate script: %w", err)
	}
	// Only reload the daemon, we don't need to start the service now.
	if err := systemd.DaemonReload(); err != nil {
		return fmt.Errorf("failed to reload systemd daemon: %w", err)
	}
	return nil
}
func (l LogRotate) Status() (common.Status, error) {
	status := common.Status{
		Service:    l.Name(),
		ConfigLoc:  l.ConfFileLocation,
		LogFileLoc: "", // logrotate itself does not have a log file, but you can set this if you want
		BinaryLoc:  filepath.Dir(l.LogrotateScript),
	}

	status.ServiceFileLoc = l.SystemdTimerFileLocation
	props, err := systemd.Show(filepath.Base(l.SystemdTimerFileLocation), "LoadState", "SubState",
		"ActiveState", "ActiveEnterTimestamp", "ActiveExitTimestamp")
	if err != nil {
		log.Error("Failed to get status of " + l.Name() + ": " + err.Error())
		return status, err
	}
	if props["LoadState"] == "not-found" {
		status.Status = common.StatusNotInstalled
		// Timers can be in the waiting state
	} else if props["SubState"] == "running" || props["SubState"] == "waiting" {
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
func (l LogRotate) Start() error {
	serviceName := filepath.Base(l.SystemdTimerFileLocation)
	log.Debug("Starting logrotate service timer " + serviceName)
	if err := systemd.DaemonReload(); err != nil {
		return fmt.Errorf("failed to start logrotate: %w", err)
	}
	if err := systemd.Enable(true, serviceName); err != nil {
		return fmt.Errorf("failed to start logrotate: %w", err)
	}
	return nil
}
func (l LogRotate) Stop() error {
	serviceName := filepath.Base(l.SystemdTimerFileLocation)
	log.Debug("Stopping logrotate service timer " + serviceName)
	if err := systemd.Stop(serviceName); err != nil {
		return fmt.Errorf("failed to stop logrotate: %w", err)
	}
	if err := systemd.Disable(serviceName); err != nil {
		return fmt.Errorf("failed to stop logrotate: %w", err)
	}
	return nil
}
func (l LogRotate) Restart() error {
	status, err := l.Status()
	if err != nil {
		return fmt.Errorf("failed to get logrotate status: %w", err)
	}
	if status.Status == common.StatusRunning {
		if err := l.Stop(); err != nil {
			return fmt.Errorf("failed to stop logrotate service: %w", err)
		}
	}
	if err := l.Start(); err != nil {
		return fmt.Errorf("failed to start logrotate service: %w", err)
	}
	return nil
}
func (l LogRotate) Install() error {
	if err := template.GenerateTemplate(l); err != nil {
		return fmt.Errorf("failed to generate logrotate template: %w", err)
	}
	// Make the logrotate.sh script executable
	if err := os.Chmod(l.LogrotateScript, 0755); err != nil {
		return fmt.Errorf("failed to set permissions on logrotate script: %w", err)
	}
	return nil
}

func (l LogRotate) Reconfigure() error {
	log.Debug("Reconfiguring logrotate service")
	if err := template.GenerateTemplate(l); err != nil {
		return fmt.Errorf("failed to reconfigure logrotate: %w", err)
	}
	// Make the logrotate.sh script executable
	if err := os.Chmod(l.LogrotateScript, 0755); err != nil {
		return fmt.Errorf("failed to set permissions on logrotate script: %w", err)
	}
	if err := systemd.DaemonReload(); err != nil {
		return fmt.Errorf("failed to reload systemd daemon: %w", err)
	}
	return nil
}

func (l LogRotate) Initialize() error {
	return l.Start()
}
func (l LogRotate) PreUpgrade() error { return nil }
