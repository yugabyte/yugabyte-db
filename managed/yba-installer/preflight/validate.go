/*
* Copyright (c) YugaByte, Inc.
 */

package preflight

import (
	"strings"

	log "github.com/yugabyte/yugabyte-db/managed/yba-installer/logging"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/common"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/config"
)


// ValidateUserPostgres validates if user postgres is initialized.
func ValidateUserPostgres(filename string) bool {

	port := config.GetYamlPathData("postgres.port")
	username := config.GetYamlPathData(".postgres.username")
	password := config.GetYamlPathData(".postgres.password")

	// Logging parsed user provided port, username, and password for
	// debugging purposes.
	log.Debug("User provided Postgres port: " + port)

	checkPostgresCommand := "PGPASSWORD= " + password +
		" psql -p " + port + " -U " + username + " -d yugaware -c '\\d'"

	command := "bash"
	args := []string{"-c", checkPostgresCommand}

	_, err := common.ExecuteBashCommand(command, args)

	if err != nil {

		log.Info("User provided Postgres not initialized properly! See the" +
			" above error message for more details.")
		return false

	}

	return true

}

// ValidateUserPython validates if python3 is present on the system and matches provided version.
func ValidateUserPython(filename string) (valid bool) {

	pythonPath := config.GetYamlPathData(".python.path")
	pythonVersion := config.GetYamlPathData(".python.version")

	log.Debug("User provided Python path: " + pythonPath)
	log.Debug("User provided Python version: " + pythonVersion)

	command := "bash"
	args := []string{"-c", "command -v python3"}
	output, _ := common.ExecuteBashCommand(command, args)

	outputTrimmed := strings.TrimSuffix(output, "\n")

	// /usr not appended to path on a print.

	if "/usr"+outputTrimmed != pythonPath {

		log.Info("User provided Python path does not match " +
			"actual Python path.")

		return false

	}

	command = "bash"
	args = []string{"-c", "python3 --version"}
	output, _ = common.ExecuteBashCommand(command, args)

	outputTrimmed = strings.TrimSuffix(output, "\n")

	if !(strings.Contains(outputTrimmed, pythonVersion)) {

		log.Info("User provided Python version does not match " +
			"actual Python version.")

		return false

	}

	return true

}
