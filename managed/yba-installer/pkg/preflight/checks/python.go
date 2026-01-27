/*
 * Copyright (c) YugabyteDB, Inc.
 */

package checks

import (
	"fmt"
	"regexp"
	"strconv"
	"strings"

	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/common/shell"
	log "github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/logging"
)

// Python checks to ensure the correct version of python exists
var Python = &pythonCheck{"python", false}

var pythonBinaryNames = []string{
	"python3",
	"python3.10",
	"python3.11",
	"python3.12"}

var pythonVersionRegex = regexp.MustCompile(`Python (\d+)\.(\d+)`)

type pythonCheck struct {
	name        string
	skipAllowed bool
}

// Name gets the name of the check
func (p pythonCheck) Name() string {
	return p.name
}

// SkipAllowed gets if the check can be skipped
func (p pythonCheck) SkipAllowed() bool {
	return p.skipAllowed
}

// Execute runs the python check. Ensures we have a valid version of python for yba.
func (p pythonCheck) Execute() Result {
	res := Result{
		Check:  p.name,
		Status: StatusPassed,
	}

	for _, binary := range pythonBinaryNames {
		log.Debug("checking for python binary " + binary)
		out := shell.Run(binary, "--version")
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
		// Allowing python 3.10 or 3.11, as defined by LINUX_PLATFORMS in common.sh
		if majorVersion == 3 && minorVersion >= 10 && minorVersion <= 12 {
			log.Info("System meets Python installation requirements with version " + outputTrimmed)
			return res
		}
		log.Warn("Found " + outputTrimmed + " on system but is not allowed Python version.")
	}

	res.Error = fmt.Errorf("System does not meet Python requirements. Please install Python " +
		"3.10, 3.11, or 3.12.")
	res.Status = StatusCritical
	return res
}
