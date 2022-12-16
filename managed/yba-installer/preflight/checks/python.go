/*
 * Copyright (c) YugaByte, Inc.
 */

package checks

import (
	"fmt"
	"regexp"
	"strings"

	"github.com/yugabyte/yugabyte-db/managed/yba-installer/common"
	log "github.com/yugabyte/yugabyte-db/managed/yba-installer/logging"
)

// Python checks to ensure the correct version of python exists
var Python = &pythonCheck{"python", false}

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
	command := "bash"
	args := []string{"-c", "python3 --version"}
	output, _ := common.ExecuteBashCommand(command, args)

	outputTrimmed := strings.TrimSuffix(output, "\n")

	re := regexp.MustCompile(`Python 3.6|Python 3.7|Python 3.8|Python 3.9`)

	if !re.MatchString(outputTrimmed) {
		res.Error = fmt.Errorf("System does not meet Python requirements. Please install any " +
			"version of Python between 3.6 and 3.9.")
		res.Status = StatusCritical
		return res
	} else {
		log.Info("System meets Python installation requirements.")
	}

	return res
}
