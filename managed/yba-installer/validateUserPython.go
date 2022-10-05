/*
* Copyright (c) YugaByte, Inc.
 */

package main

import (
	"strings"
)

// Reads info from input config file and sets all template parameters
// for each individual config file (for every component separately)
func ValidateUserPython(filename string) (valid bool) {

	pythonPath := getYamlPathData(".python.path")
	pythonVersion := getYamlPathData(".python.version")

	LogDebug("User provided Python path: " + pythonPath)
	LogDebug("User provided Python version: " + pythonVersion)

	command := "bash"
	args := []string{"-c", "command -v python3"}
	output, _ := ExecuteBashCommand(command, args)

	outputTrimmed := strings.TrimSuffix(output, "\n")

	// /usr not appended to path on a print.

	if "/usr"+outputTrimmed != pythonPath {

		LogInfo("User provided Python path does not match " +
			"actual Python path.")

		return false

	}

	command = "bash"
	args = []string{"-c", "python3 --version"}
	output, _ = ExecuteBashCommand(command, args)

	outputTrimmed = strings.TrimSuffix(output, "\n")

	if !(strings.Contains(outputTrimmed, pythonVersion)) {

		LogInfo("User provided Python version does not match " +
			"actual Python version.")

		return false

	}

	return true

}
