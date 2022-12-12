/*
* Copyright (c) YugaByte, Inc.
 */

package checks

import (
	"fmt"

	"github.com/spf13/viper"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/common"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/config"
	log "github.com/yugabyte/yugabyte-db/managed/yba-installer/logging"
)

// Postgres checks if we can connect to postgres
var Postgres = &postgresCheck{"postgres", true}

type postgresCheck struct {
	name        string
	skipAllowed bool
}

// Name gets the name of the check
func (p postgresCheck) Name() string {
	return p.name
}

// SkipAllowed returns if the check can be skipped
func (p postgresCheck) SkipAllowed() bool {
	return p.skipAllowed
}

// ValidateUserPostgres validates if user postgres is initialized.
func (p postgresCheck) Execute() Result {
	res := Result{
		Check:  p.name,
		Status: StatusPassed,
	}
	if !viper.GetBool("postgres.bringOwn") {
		log.Debug("skipping " + p.name + " as user we will provide postgres")
		return res
	}
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

	if _, err := common.ExecuteBashCommand(command, args); err != nil {
		log.Info("User provided Postgres not initialized properly! See the" +
			" above error message for more details.")
		res.Error = fmt.Errorf("Could not connect to postgres")
		res.Status = StatusCritical
	}

	return res
}
