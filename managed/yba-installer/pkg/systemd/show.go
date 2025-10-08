/*
 * Copyright (c) YugabyteDB, Inc.
 *
 * This is mainly a placeholder until we integrate with a systemd library.
 * For now, it will contain helper functions for performing basic systemctl
 * commands.
 */
package systemd

import (
	"bytes"
	"fmt"
	"os/exec"
	"strings"

	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/logging"
)

// Show will parse systemctl show output for the given service.
// One can provide specific properties to get the status of, otherwise
// all properties are given.
func Show(service string, properties ...string) (map[string]string, error) {
	args := []string{
		"show",
		service,
	}
	if len(properties) > 0 {
		args = append(args, "--property", strings.Join(properties, ","))
	}
	if !isRoot() {
		args = append(args, "--user")
	}
	logging.Info(fmt.Sprintf("systemctl %s", strings.Join(args, " ")))
	cmd := exec.Command("systemctl", args...)
	var stderr bytes.Buffer
	cmd.Stderr = &stderr
	stdout, err := cmd.Output()
	if err != nil {
		logging.Error("failed running systemctl show, stdout: " + string(stdout))
		logging.Error("failed running systemctl show, stderr: " + stderr.String())
		return nil, fmt.Errorf("failed to run systemctl %s: %w", args, err)
	}

	var propMap = make(map[string]string)
	for _, line := range strings.Split(string(stdout), "\n") {
		val := strings.SplitN(line, "=", 2)
		// While this could <technically> be a mis-formed lined, really it is just a trailing new line,
		// so it is safe to skip.
		if len(val) < 2 {
			continue
		}
		propMap[val[0]] = strings.Trim(val[1], " \n")
	}
	return propMap, nil
}
