/*
 * Copyright (c) YugaByte, Inc.
 */

package checks

import (
	"fmt"
	"regexp"
	"strings"

	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/common/shell"
	log "github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/logging"
)

// Python checks to ensure the correct version of python exists
var Python = &pythonCheck{"python", false}

var pythonBinaryNames = []string{
	"python", "python3", "python3.8", "python3.9", "python3.10", "python3.11",
}

var pythonVersionRegex = regexp.MustCompile(`Python 3.8|Python 3.9|Python 3.10|Python 3.11`)

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
		outputTrimmed := strings.TrimSuffix(out.StdoutString(), "\n")
		if pythonVersionRegex.MatchString(outputTrimmed) {
			log.Info("System meets Python installation requirements with version " + outputTrimmed)
			return res
		}
	}

	res.Error = fmt.Errorf("System does not meet Python requirements. Please install any " +
		"version of Python between 3.8 and 3.11.")
	res.Status = StatusCritical
	return res
}
