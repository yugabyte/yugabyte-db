/*
 * Copyright (c) YugaByte, Inc.
 */

package checks

import (
	"fmt"
	"regexp"
	"strings"

	"github.com/spf13/viper"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/common"
	log "github.com/yugabyte/yugabyte-db/managed/yba-installer/logging"
)

var Python = &pythonCheck{"python", "critical"}

type pythonCheck struct {
	name         string
	warningLevel string
}

func (p pythonCheck) Name() string {
	return p.name
}

func (p pythonCheck) WarningLevel() string {
	return p.warningLevel
}

func (p pythonCheck) Execute() error {
	command := "bash"
	args := []string{"-c", "python3 --version"}
	output, _ := common.ExecuteBashCommand(command, args)

	outputTrimmed := strings.TrimSuffix(output, "\n")

	re := regexp.MustCompile(`Python 3.6|Python 3.7|Python 3.8|Python 3.9`)

	if !re.MatchString(outputTrimmed) {
		return fmt.Errorf("System does not meet Python requirements. Please install any " +
			"version of Python between 3.6 and 3.9.")
	} else {
		log.Info("System meets Python installation requirements.")
	}

	// Check if the user defined python is the expected version.
	if viper.GetBool("python.bringOwn") {
		re = regexp.MustCompile(viper.GetString("python.version"))
		if !re.MatchString(outputTrimmed) {
			return fmt.Errorf("User defined python %s not found. Got %s",
				viper.GetString("python.version"), outputTrimmed)
		}
	}
	return nil
}
