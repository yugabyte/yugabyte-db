package systemd

import (
	"fmt"
	"os/exec"
	"strconv"
	"strings"

	"github.com/spf13/viper"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/common/shell"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/logging"
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

// Version returns the systemd version on the system
func Version() (int, error) {
	// Run the 'systemctl --version' command
	output := shell.Run("systemctl", "--version")
	if !output.SucceededOrLog() {
		return 0, fmt.Errorf("unable to run systemctl --version")
	}

	// Get the first line of the output, which contains the version number
	lines := strings.Split(output.StdoutString(), "\n")
	if len(lines) == 0 {
		return 0, fmt.Errorf("unable to parse systemd version")
	}

	// Extract the version number from the first line
	fields := strings.Fields(lines[0])
	if len(fields) < 2 {
		return 0, fmt.Errorf("unexpected output format")
	}

	// Convert the version string to an integer
	version, err := strconv.Atoi(fields[1])
	if err != nil {
		return 0, fmt.Errorf("unable to parse version number: %v", err)
	}
	return version, nil
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
	if viper.IsSet("as_root") {
		return viper.GetBool("as_root")
	}
	logging.Debug("as_root not set, checking user id")
	cmd := exec.Command("id", "-u")
	output, err := cmd.Output()
	if err != nil {
		logging.Fatal("Error: " + err.Error() + ".")
	}

	i, err := strconv.Atoi(string(output[:len(output)-1]))
	if err != nil {
		logging.Fatal("Error: " + err.Error() + ".")
	}

	if i == 0 {
		return true
	}
	return false
}
