/*
 * Copyright (c) YugaByte, Inc.
 */

package preflight

import (
	"regexp"
	"strings"

	"github.com/yugabyte/yugabyte-db/managed/yba-installer/common"
	log "github.com/yugabyte/yugabyte-db/managed/yba-installer/logging"
)

var python = Python{"python", "critical"}

type Python struct {
	name         string
	warningLevel string
}

func (p Python) Name() string {
	return p.name
}

func (p Python) WarningLevel() string {
	return p.warningLevel
}

func (p Python) Execute() {
	command := "bash"
	args := []string{"-c", "python3 --version"}
	output, _ := common.ExecuteBashCommand(command, args)

	outputTrimmed := strings.TrimSuffix(output, "\n")

	re := regexp.MustCompile(`Python 3.6|Python 3.7|Python 3.8|Python 3.9`)

	if !re.MatchString(outputTrimmed) {

		log.Fatal("System does not meet Python requirements. Please install any " +
			"version of Python between 3.6 and 3.9 to continue.")

	} else {

		log.Info("System meets Python installation requirements.")
	}
}

func init() {
	RegisterPreflightCheck(python)
}
