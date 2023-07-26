package systemd

import (
	"fmt"
	"strings"

	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/common/shell"
)

// Stop the given systemctl services
func Stop(serviceNames ...string) error {
	return runSysctlCmd("stop", serviceNames)
}

// Disable (don't restart on system reboot) the given systemctl services
func Disable(serviceNames ...string) error {
	return runSysctlCmd("disable", serviceNames)
}

// Start the given systemctl services
// enable == true will run with --now, ensuring the services are started on reboot.
func Start(serviceNames ...string) error {
	return runSysctlCmd("start", serviceNames)
}

// Enable (restart the services on system reboot) the given systemctl services.
func Enable(now bool, serviceNames ...string) error {
	if now {
		return runSysctlCmd("enable", serviceNames, "--now")
	}
	return runSysctlCmd("enable", serviceNames)
}

// DaemonReload performs systemctl daemon-reload
func DaemonReload() error {
	if out := shell.Run("systemctl", "daemon-reload"); !out.SucceededOrLog() {
		return fmt.Errorf("systemctl daemon-reload failed: %w", out.Error)
	}
	return nil
}

// runSysctlCmd is a helper for running some basic systemctl commands
// cmd: systemctl cmd to run
// services: list of services to perform the command against
// args: optional additional arguments to
func runSysctlCmd(cmd string, services []string, args ...string) error {
	cmdArgs := append([]string{cmd}, services...)
	cmdArgs = append(cmdArgs, args...)
	out := shell.Run("systemctl", cmdArgs...)
	if !out.SucceededOrLog() {
		return fmt.Errorf("systemctl %s on services '%s' failed: %w", cmd,
			strings.Join(services, ", "), out.Error)
	}
	return nil
}
