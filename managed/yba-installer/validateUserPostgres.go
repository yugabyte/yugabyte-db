/*
* Copyright (c) YugaByte, Inc.
 */

package main

import ()

// Reads info from input config file and sets all template parameters
// for each individual config file (for every component separately)
func ValidateUserPostgres(filename string) bool {

	port := getYamlPathData(".postgres.port")
	username := getYamlPathData(".postgres.username")
	password := getYamlPathData(".postgres.password")

	// Logging parsed user provided port, username, and password for
	// debugging purposes.
	LogDebug("User provided Postgres port: " + port)

	checkPostgresCommand := "PGPASSWORD= " + password +
		" psql -p " + port + " -U " + username + " -d yugaware -c '\\d'"

	command := "bash"
	args := []string{"-c", checkPostgresCommand}

	_, err := ExecuteBashCommand(command, args)

	if err != nil {

		LogInfo("User provided Postgres not initialized properly! See the" +
			" above error message for more details.")
		return false

	}

	return true

}
