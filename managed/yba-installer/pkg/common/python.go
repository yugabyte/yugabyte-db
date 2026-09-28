/*
 * Copyright (c) YugabyteDB, Inc.
 */

package common

import (
	"fmt"
	"regexp"
	"strconv"
	"strings"

	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/common/shell"
	log "github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/logging"
)

// Ordered as common.sh probes them.
var pythonBinaryNames = []string{
	"python3.10",
	"python3.11",
	"python3.12",
	"python3.13",
	"python3"}

var pythonVersionRegex = regexp.MustCompile(`Python (\d+)\.(\d+)`)

// ValidatePython checks that a python version YBA supports is runnable by the given user. YBA runs
// the devops scripts as the service user, so that user's python is the one that matters: a python
// directory the service user cannot traverse makes common.sh skip that interpreter and silently
// fall back to an unsupported system python3. An empty user runs the check in process, for
// non-root installs where the services run as the installing user.
func ValidatePython(user string) error {
	who := "the installing user"
	if user != "" {
		who = "user '" + user + "'"
	}
	for _, binary := range pythonBinaryNames {
		log.Debug("checking for python binary " + binary + " as " + who)
		var out *shell.Output
		if user == "" {
			out = shell.Run(binary, "--version")
		} else {
			out = shell.RunAsUser(user, binary, "--version")
		}
		if !out.Succeeded() {
			log.Debug("python binary " + binary + " failed, trying next")
			continue
		}
		outputTrimmed := strings.TrimSpace(out.StdoutString())
		match := pythonVersionRegex.FindStringSubmatch(outputTrimmed)
		if len(match) < 3 {
			continue
		}
		majorVersion, _ := strconv.Atoi(match[1])
		minorVersion, _ := strconv.Atoi(match[2])
		// Allowed versions are defined by LINUX_PLATFORMS in common.sh
		if majorVersion == 3 && minorVersion >= 10 && minorVersion <= 13 {
			log.Info(who + " meets Python installation requirements with version " + outputTrimmed)
			return nil
		}
		log.Warn("Found " + outputTrimmed + " for " + who + " but is not an allowed Python version.")
	}
	return fmt.Errorf("%s does not meet Python requirements. Please install Python 3.10, 3.11, "+
		"3.12, or 3.13 and ensure %s can run it", who, who)
}
