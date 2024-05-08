package systemd

import (
	"fmt"
	osuser "os/user"

	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/common/shell"
)

// Stop the given systemctl services
func Stop(serviceNames ...string) error {
	return runSysctlServiceCmd("stop", serviceNames)
}

// Disable (don't restart on system reboot) the given systemctl services
func Disable(serviceNames ...string) error {
	return runSysctlServiceCmd("disable", serviceNames)
}

// Start the given systemctl services
// enable == true will run with --now, ensuring the services are started on reboot.
func Start(serviceNames ...string) error {
	return runSysctlServiceCmd("start", serviceNames)
}

func Restart(serviceNames ...string) error {
	return runSysctlServiceCmd("restart", serviceNames)
}

// Enable (restart the services on system reboot) the given systemctl services.
func Enable(now bool, serviceNames ...string) error {
	if now {
		return runSysctlServiceCmd("enable", serviceNames, "--now")
	}
	return runSysctlServiceCmd("enable", serviceNames)
}

// Link will create a link to the provided target service
func Link(target string) error {
	return runSysctlCmd("link", target)
}

// DaemonReload performs systemctl daemon-reload
func DaemonReload() error {
	return runSysctlCmd("daemon-reload")
}

// runSysctlServiceCmd is a helper for running some basic systemctl commands
// cmd: systemctl cmd to run
// services: list of services to perform the command against
// args: optional additional arguments to
func runSysctlServiceCmd(cmd string, services []string, args ...string) error {
	return runSysctlCmd(cmd, append(services, args...)...)
}

func runSysctlCmd(cmd string, args ...string) error {
	cmdArgs := []string{cmd}
	if !isRoot() {
		cmdArgs = append([]string{"--user"}, cmdArgs...)
	}
	cmdArgs = append(cmdArgs, args...)
	if out := shell.Run("systemctl", cmdArgs...); !out.SucceededOrLog() {
		return fmt.Errorf("systemctl %s failed: %w", cmd, out.Error)
	}
	return nil
}

func isRoot() bool {
	user, err := osuser.Current()
	if err != nil {
		panic(err)
	}
	return user.Uid == "0"
}
